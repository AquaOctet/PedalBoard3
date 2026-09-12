#include "TakeWriter.h"

#include "engine/Recordings.h"

#include <algorithm>

namespace pedalboard3::engine
{

TakeWriter::TakeWriter() : audioFifo_(kRingFrames), eventFifo_(kEventSlots)
{
    ring_.setSize(2, kRingFrames);
}

TakeWriter::~TakeWriter()
{
    // Keep what was recorded: write out what's left and close the take.
    pump();
    closeTake();
}

void TakeWriter::setSampleRate(double rate) noexcept
{
    if (rate > 0.0)
        sampleRate_.store(rate, std::memory_order_relaxed);
}

void TakeWriter::startTake() noexcept
{
    pushEvent(true);
}

void TakeWriter::endTake() noexcept
{
    pushEvent(false);
}

bool TakeWriter::push(const juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    const int channels = juce::jmin(2, buffer.getNumChannels());
    if (numSamples <= 0 || channels == 0)
        return true;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    audioFifo_.prepareToWrite(numSamples, start1, size1, start2, size2);
    if (size1 + size2 < numSamples)
    {
        dropped_.store(true, std::memory_order_relaxed); // the disk fell too far behind
        return false;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        const int from = juce::jmin(ch, channels - 1); // a mono input fills both sides
        if (size1 > 0)
            ring_.copyFrom(ch, start1, buffer, from, start, size1);
        if (size2 > 0)
            ring_.copyFrom(ch, start2, buffer, from, start + size1, size2);
    }
    audioFifo_.finishedWrite(size1 + size2);
    framesPushed_.fetch_add(numSamples, std::memory_order_release);
    return true;
}

void TakeWriter::pushEvent(bool start) noexcept
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

void TakeWriter::setFolder(const juce::File& folder)
{
    const std::lock_guard<std::mutex> lk(mutex_);
    folder_ = folder;
}

juce::File TakeWriter::getFolder() const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    return folder_ != juce::File() ? folder_ : recordings::defaultFolder();
}

juce::File TakeWriter::getChosenFolder() const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    return folder_;
}

void TakeWriter::setPrefix(const juce::String& prefix)
{
    const std::lock_guard<std::mutex> lk(mutex_);
    prefix_ = prefix;
}

juce::File TakeWriter::getLastTake() const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    return lastTake_;
}

juce::String TakeWriter::getError() const
{
    const std::lock_guard<std::mutex> lk(mutex_);
    return error_;
}

void TakeWriter::pump()
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

void TakeWriter::drain(std::int64_t frames)
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

void TakeWriter::openTake()
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

void TakeWriter::closeTake()
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

} // namespace pedalboard3::engine
