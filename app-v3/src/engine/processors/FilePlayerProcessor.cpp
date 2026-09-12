#include "FilePlayerProcessor.h"

#include "engine/DiskThread.h"

#include <cmath>

namespace pedalboard3::engine::processors
{

namespace
{
constexpr int kIdleSliceMs = 200;

std::vector<float> overviewOf(const juce::AudioBuffer<float>& audio, int points)
{
    std::vector<float> peaks;
    const int length = audio.getNumSamples();
    if (length <= 0)
        return peaks;
    peaks.resize(static_cast<std::size_t>(points), 0.0f);
    for (int p = 0; p < points; ++p)
    {
        const int start = static_cast<int>(static_cast<std::int64_t>(length) * p / points);
        const int end = juce::jmax(start + 1, static_cast<int>(static_cast<std::int64_t>(length) * (p + 1) / points));
        float peak = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            peak = juce::jmax(peak, audio.getMagnitude(ch, start, juce::jmin(end, length) - start));
        peaks[static_cast<std::size_t>(p)] = juce::jmin(1.0f, peak);
    }
    return peaks;
}
} // namespace

FilePlayerProcessor::FilePlayerProcessor(DiskThread& disk)
    : ProcessorBase(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    "FilePlayerProcessor"),
      disk_(disk)
{
    addParameter(play_ = new juce::AudioParameterBool(juce::ParameterID{"play", 1}, "Play", false));
    addParameter(returnToZero_ =
                     new juce::AudioParameterBool(juce::ParameterID{"returnToZero", 1}, "Return to Zero", false));
    addParameter(trigger_ = new juce::AudioParameterBool(juce::ParameterID{"trigger", 1}, "Trigger", false));
    addParameter(looping_ = new juce::AudioParameterBool(juce::ParameterID{"looping", 1}, "Loop", false));
    addParameter(sync_ = new juce::AudioParameterBool(juce::ParameterID{"sync", 1}, "Sync to Transport", false));
    for (auto* trigger : {play_, returnToZero_, trigger_})
        trigger->addListener(this);

    disk_.thread().addTimeSliceClient(this);
}

FilePlayerProcessor::~FilePlayerProcessor()
{
    disk_.thread().removeTimeSliceClient(this); // waits if a load is running
    for (auto* trigger : {play_, returnToZero_, trigger_})
        trigger->removeListener(this);
}

void FilePlayerProcessor::prepareToPlay(double sampleRate, int)
{
    const double previous = deviceRate_.exchange(sampleRate, std::memory_order_acq_rel);
    // A new device rate: decode the file again at it, so synced playback
    // stays sample-for-sample with the transport.
    if (std::abs(previous - sampleRate) > 0.5 && getFile() != juce::File())
        setFile(getFile());
}

bool FilePlayerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void FilePlayerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    const juce::SpinLock::ScopedTryLockType lock(swapLock_);
    if (!lock.isLocked() || loaded_ == nullptr)
    {
        audible_.store(false, std::memory_order_relaxed);
        return; // pass the input through: no file, or a new one is being swapped in
    }
    const auto& audio = loaded_->audio;
    const std::int64_t length = audio.getNumSamples();
    if (length <= 0)
        return;

    if (const auto seek = seekRequest_.exchange(-1, std::memory_order_acq_rel); seek >= 0)
        position_.store(juce::jmin(seek, length - 1), std::memory_order_relaxed);

    const bool looping = looping_->get();
    const bool synced = sync_->get();
    bool playing = false;
    std::int64_t index = 0;
    if (synced)
    {
        // The transport's position, in samples at the device rate: the file is
        // decoded at that rate, so it maps one to one.
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
            {
                playing = position->getIsPlaying();
                index = juce::jmax<std::int64_t>(0, position->getTimeInSamples().orFallback(0));
            }
        if (looping)
            index %= length;
    }
    else
    {
        playing = playing_.load(std::memory_order_relaxed);
        index = position_.load(std::memory_order_relaxed);
    }
    shownPosition_.store(juce::jmin(index, length), std::memory_order_relaxed);
    audible_.store(playing && index < length, std::memory_order_relaxed);
    if (!playing)
        return;

    const int fileChannels = audio.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    for (int n = 0; n < numSamples; ++n)
    {
        if (index >= length)
        {
            if (!looping)
                break;
            index = 0;
        }
        const int at = static_cast<int>(index);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample(ch, n, audio.getSample(juce::jmin(ch, fileChannels - 1), at));
        ++index;
    }

    if (!synced)
    {
        if (index >= length && !looping)
        {
            // The end: stop and rewind, as Pedalboard2 did.
            playing_.store(false, std::memory_order_relaxed);
            position_.store(0, std::memory_order_relaxed);
        }
        else
        {
            position_.store(index % length, std::memory_order_relaxed);
        }
    }
}

void FilePlayerProcessor::setFile(const juce::File& file)
{
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        file_ = file;
        loadRequested_ = true;
        error_.clear();
    }
    loading_.store(true, std::memory_order_release);
    disk_.thread().moveToFrontOfQueue(this);
}

juce::File FilePlayerProcessor::getFile() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return file_;
}

void FilePlayerProcessor::togglePlay() noexcept
{
    playing_.store(!playing_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void FilePlayerProcessor::returnToZero() noexcept
{
    seekRequest_.store(0, std::memory_order_release);
}

void FilePlayerProcessor::seekTo(double fraction) noexcept
{
    const auto length = length_.load(std::memory_order_relaxed);
    if (length > 0)
        seekRequest_.store(
            static_cast<std::int64_t>(juce::jlimit(0.0, 1.0, fraction) * static_cast<double>(length - 1)),
            std::memory_order_release);
}

double FilePlayerProcessor::getLengthSeconds() const noexcept
{
    const double rate = loadedRate_.load(std::memory_order_relaxed);
    return rate > 0.0 ? static_cast<double>(length_.load(std::memory_order_relaxed)) / rate : 0.0;
}

double FilePlayerProcessor::getPositionSeconds() const noexcept
{
    const double rate = loadedRate_.load(std::memory_order_relaxed);
    return rate > 0.0 ? static_cast<double>(shownPosition_.load(std::memory_order_relaxed)) / rate : 0.0;
}

juce::String FilePlayerProcessor::getLoadError() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return error_;
}

std::vector<float> FilePlayerProcessor::getOverview() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return overview_;
}

juce::MemoryBlock FilePlayerProcessor::stateForFile(const juce::File& file)
{
    juce::XmlElement xml("PB3_PROCESSOR");
    xml.setAttribute("type", "FilePlayerProcessor");
    xml.setAttribute("version", 1);
    xml.setAttribute("file", file.getFullPathName());
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(xml, state);
    return state;
}

void FilePlayerProcessor::writeState(juce::XmlElement& xml) const
{
    xml.setAttribute("file", getFile().getFullPathName());
}

void FilePlayerProcessor::readState(const juce::XmlElement& xml)
{
    const auto path = xml.getStringAttribute("file");
    setFile(juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File());
}

void FilePlayerProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    // Called on the thread that set the value (the message thread, for a
    // mapping or the window). A press is the value switching on; the release
    // does nothing.
    if (newValue < 0.5f)
        return;
    if (parameterIndex == play_->getParameterIndex())
    {
        togglePlay();
    }
    else if (parameterIndex == returnToZero_->getParameterIndex())
    {
        returnToZero();
    }
    else if (parameterIndex == trigger_->getParameterIndex())
    {
        returnToZero();
        playing_.store(true, std::memory_order_relaxed);
    }
}

int FilePlayerProcessor::useTimeSlice()
{
    juce::File file;
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        if (!loadRequested_)
            return kIdleSliceMs;
        loadRequested_ = false;
        file = file_;
    }
    load(file, deviceRate_.load(std::memory_order_acquire));
    return 0; // another request may have arrived meanwhile
}

void FilePlayerProcessor::load(const juce::File& file, double deviceRate)
{
    std::unique_ptr<Loaded> fresh;
    juce::String error;
    if (file != juce::File())
    {
        std::unique_ptr<juce::AudioFormatReader> reader(disk_.formats().createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
        {
            error = "Couldn't read \"" + file.getFileName() + "\"";
        }
        else if (static_cast<double>(reader->lengthInSamples) / reader->sampleRate > kMaxSeconds)
        {
            error = "\"" + file.getFileName() + "\" is longer than 30 minutes";
        }
        else
        {
            const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
            const int sourceLength = static_cast<int>(reader->lengthInSamples);
            juce::AudioBuffer<float> source(channels, sourceLength);
            reader->read(&source, 0, sourceLength, 0, true, channels > 1);

            fresh = std::make_unique<Loaded>();
            const double targetRate = deviceRate > 0.0 ? deviceRate : reader->sampleRate;
            fresh->sampleRate = targetRate;
            if (std::abs(reader->sampleRate - targetRate) < 0.5)
            {
                fresh->audio = std::move(source);
            }
            else
            {
                // Resample once, here, so the audio thread plays sample for sample.
                const double ratio = reader->sampleRate / targetRate;
                const int length = static_cast<int>(std::ceil(sourceLength / ratio));
                fresh->audio.setSize(channels, length);
                for (int ch = 0; ch < channels; ++ch)
                {
                    juce::LagrangeInterpolator interpolator;
                    interpolator.process(
                        ratio, source.getReadPointer(ch), fresh->audio.getWritePointer(ch), length, sourceLength, 0);
                }
            }
        }
    }

    auto overview = fresh != nullptr ? overviewOf(fresh->audio, kOverviewPoints) : std::vector<float>{};
    const std::int64_t length = fresh != nullptr ? fresh->audio.getNumSamples() : 0;
    const double rate = fresh != nullptr ? fresh->sampleRate : 0.0;
    {
        const juce::SpinLock::ScopedLockType lock(swapLock_);
        std::swap(loaded_, fresh);
        length_.store(length, std::memory_order_relaxed);
        loadedRate_.store(rate, std::memory_order_relaxed);
        position_.store(0, std::memory_order_relaxed);
        shownPosition_.store(0, std::memory_order_relaxed);
    }
    fresh.reset(); // the previous file, freed here rather than on the audio thread

    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        error_ = error;
        overview_ = std::move(overview);
    }
    overviewGeneration_.fetch_add(1, std::memory_order_acq_rel);
    loading_.store(false, std::memory_order_release);
}

} // namespace pedalboard3::engine::processors
