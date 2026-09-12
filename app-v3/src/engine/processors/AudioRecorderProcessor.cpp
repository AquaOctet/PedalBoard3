#include "AudioRecorderProcessor.h"

#include "engine/DiskThread.h"
#include "engine/Recordings.h"

#include <algorithm>

namespace pedalboard3::engine::processors
{

namespace
{
constexpr int kRingFrames = 4 * 96000; // 4 s at 96 kHz, 8 s at 48 kHz
constexpr int kEventSlots = 64;
constexpr int kBusySliceMs = 10;
constexpr int kIdleSliceMs = 100;
} // namespace

AudioRecorderProcessor::AudioRecorderProcessor(DiskThread& disk)
    : ProcessorBase(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    "AudioRecorderProcessor"),
      disk_(disk), audioFifo_(kRingFrames), eventFifo_(kEventSlots)
{
    ring_.setSize(2, kRingFrames);
    addParameter(record_ = new juce::AudioParameterBool(juce::ParameterID{"record", 1}, "Record", false));
    addParameter(sync_ = new juce::AudioParameterBool(juce::ParameterID{"sync", 1}, "Sync to Transport", false));
    record_->addListener(this);
    disk_.thread().addTimeSliceClient(this);
}

AudioRecorderProcessor::~AudioRecorderProcessor()
{
    // Keep what was recorded: detach from the disk thread (waiting out a slice
    // in progress), then write out what's left and close the take here.
    wantRecording_.store(false, std::memory_order_relaxed);
    disk_.thread().removeTimeSliceClient(this);
    record_->removeListener(this);
    pump();
    closeTake();
}

void AudioRecorderProcessor::prepareToPlay(double sampleRate, int)
{
    if (sampleRate > 0.0)
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
}

bool AudioRecorderProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AudioRecorderProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin(2, buffer.getNumChannels());
    if (channels == 0 || numSamples <= 0)
        return;

    float peak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        peak = juce::jmax(peak, buffer.getMagnitude(ch, 0, numSamples));
    if (peak > peak_.load(std::memory_order_relaxed))
        peak_.store(peak, std::memory_order_relaxed);

    // Synced: follow the transport's play / stop edges.
    if (sync_->get())
    {
        bool playing = false;
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
                playing = position->getIsPlaying();
        if (playing != lastTransportPlaying_)
            wantRecording_.store(playing, std::memory_order_relaxed);
        lastTransportPlaying_ = playing;
    }

    const bool want = wantRecording_.load(std::memory_order_relaxed);
    if (!want && capturing_.load(std::memory_order_relaxed))
    {
        capturing_.store(false, std::memory_order_relaxed);
        pushEvent(false);
    }
    if (want && !capturing_.load(std::memory_order_relaxed))
    {
        capturing_.store(true, std::memory_order_relaxed);
        takeFrames_.store(0, std::memory_order_relaxed);
        pushEvent(true);
    }
    if (!capturing_.load(std::memory_order_relaxed))
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    audioFifo_.prepareToWrite(numSamples, start1, size1, start2, size2);
    if (size1 + size2 < numSamples)
    {
        dropped_.store(true, std::memory_order_relaxed); // the disk fell too far behind
        return;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        const int from = juce::jmin(ch, channels - 1); // a mono input fills both sides
        if (size1 > 0)
            ring_.copyFrom(ch, start1, buffer, from, 0, size1);
        if (size2 > 0)
            ring_.copyFrom(ch, start2, buffer, from, size1, size2);
    }
    audioFifo_.finishedWrite(size1 + size2);
    framesPushed_.fetch_add(numSamples, std::memory_order_release);
    takeFrames_.fetch_add(numSamples, std::memory_order_relaxed);
}

void AudioRecorderProcessor::pushEvent(bool start) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    eventFifo_.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 < 1)
    {
        dropped_.store(true, std::memory_order_relaxed);
        return;
    }
    events_[static_cast<std::size_t>(start1)] = {framesPushed_.load(std::memory_order_relaxed), start};
    eventFifo_.finishedWrite(1);
}

void AudioRecorderProcessor::toggleRecord() noexcept
{
    wantRecording_.store(!wantRecording_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

bool AudioRecorderProcessor::isRecording() const noexcept
{
    return capturing_.load(std::memory_order_relaxed) || wantRecording_.load(std::memory_order_relaxed);
}

double AudioRecorderProcessor::getTakeSeconds() const noexcept
{
    if (!capturing_.load(std::memory_order_relaxed))
        return 0.0;
    return static_cast<double>(takeFrames_.load(std::memory_order_relaxed)) /
           sampleRate_.load(std::memory_order_relaxed);
}

float AudioRecorderProcessor::takeInputPeak() noexcept
{
    return juce::jmin(1.0f, peak_.exchange(0.0f, std::memory_order_relaxed));
}

juce::File AudioRecorderProcessor::getLastTake() const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    return lastTake_;
}

juce::String AudioRecorderProcessor::getError() const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}

void AudioRecorderProcessor::setFolder(const juce::File& folder)
{
    const std::lock_guard<std::mutex> lk(mutex_);
    folder_ = folder;
}

juce::File AudioRecorderProcessor::getFolder() const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    return folder_ != juce::File() ? folder_ : recordings::defaultFolder();
}

void AudioRecorderProcessor::setTakePrefix(const juce::String& prefix)
{
    const std::lock_guard<std::mutex> lk(mutex_);
    prefix_ = prefix;
}

void AudioRecorderProcessor::writeState(juce::XmlElement& xml) const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    if (folder_ != juce::File())
        xml.setAttribute("folder", folder_.getFullPathName());
}

void AudioRecorderProcessor::readState(const juce::XmlElement& xml)
{
    const auto path = xml.getStringAttribute("folder");
    setFolder(juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File());
}

void AudioRecorderProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    // A mapped footswitch: the press toggles the take, the release does nothing.
    if (parameterIndex == record_->getParameterIndex() && newValue >= 0.5f)
        toggleRecord();
}

int AudioRecorderProcessor::useTimeSlice()
{
    pump();
    return writer_ != nullptr || capturing_.load(std::memory_order_relaxed) ? kBusySliceMs : kIdleSliceMs;
}

void AudioRecorderProcessor::pump()
{
    for (;;)
    {
        // The next take boundary, if the audio thread has stamped one.
        bool haveEvent = false;
        Event next;
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            eventFifo_.prepareToRead(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                haveEvent = true;
                next = events_[static_cast<std::size_t>(start1)];
            }
        }

        const auto pushed = framesPushed_.load(std::memory_order_acquire);
        const auto until = haveEvent ? std::min(next.frame, pushed) : pushed;
        drain(until - framesPulled_);

        if (haveEvent && framesPulled_ >= next.frame)
        {
            eventFifo_.finishedRead(1);
            if (next.start)
                openTake();
            else
                closeTake();
            continue;
        }
        return;
    }
}

void AudioRecorderProcessor::drain(std::int64_t frames)
{
    while (frames > 0)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        audioFifo_.prepareToRead(
            static_cast<int>(std::min<std::int64_t>(frames, kRingFrames)), start1, size1, start2, size2);
        const int got = size1 + size2;
        if (got == 0)
            return;
        if (writer_ != nullptr) // no take open (it failed to open): the audio is dropped
        {
            if (size1 > 0)
                writer_->writeFromAudioSampleBuffer(ring_, start1, size1);
            if (size2 > 0)
                writer_->writeFromAudioSampleBuffer(ring_, start2, size2);
            takeWritten_ += got;
        }
        audioFifo_.finishedRead(got);
        framesPulled_ += got;
        frames -= got;
    }
}

void AudioRecorderProcessor::openTake()
{
    closeTake(); // never two at once
    juce::File folder;
    juce::String prefix;
    {
        const std::lock_guard<std::mutex> lk(mutex_);
        folder = folder_ != juce::File() ? folder_ : recordings::defaultFolder();
        prefix = prefix_;
        error_.clear();
    }

    juce::String error;
    const auto file = recordings::newTakeFile(folder, prefix, juce::Time::getCurrentTime());
    if (!folder.createDirectory())
    {
        error = "Couldn't create " + folder.getFullPathName();
    }
    else
    {
        auto stream = std::make_unique<juce::FileOutputStream>(file);
        if (stream->failedToOpen())
        {
            error = "Couldn't write " + file.getFullPathName();
        }
        else
        {
            juce::WavAudioFormat wav;
            writer_.reset(wav.createWriterFor(
                stream.get(), sampleRate_.load(std::memory_order_relaxed), 2, kBitsPerSample, {}, 0));
            if (writer_ != nullptr)
                stream.release(); // the writer owns it
            else
                error = "Couldn't start a WAV file in " + folder.getFullPathName();
        }
    }

    if (error.isNotEmpty())
    {
        file.deleteFile();
        const std::lock_guard<std::mutex> lk(mutex_);
        error_ = error;
        return;
    }
    currentTake_ = file;
    takeWritten_ = 0;
}

void AudioRecorderProcessor::closeTake()
{
    if (writer_ == nullptr)
        return;
    writer_.reset(); // flushes and closes the file; it's never reopened
    if (takeWritten_ == 0)
    {
        currentTake_.deleteFile(); // an empty take isn't worth keeping
    }
    else
    {
        const std::lock_guard<std::mutex> lk(mutex_);
        lastTake_ = currentTake_;
        takeCount_.fetch_add(1, std::memory_order_acq_rel);
    }
    currentTake_ = juce::File();
    takeWritten_ = 0;
}

} // namespace pedalboard3::engine::processors
