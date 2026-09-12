#include "LooperProcessor.h"

#include "engine/DiskThread.h"
#include "engine/TransportMath.h"

#include <cmath>
#include <cstring>

namespace pedalboard3::engine::processors
{

namespace
{
constexpr std::uint32_t kReturnRequest = 1u << 0;
constexpr std::uint32_t kRecordRequest = 1u << 1;
constexpr int kBusySliceMs = 10;
constexpr int kIdleSliceMs = 100;
constexpr std::int64_t kHeadroomFrames = 4 * static_cast<std::int64_t>(LoopBuffer::kChunkFrames);
constexpr double kMaxRate = 192000.0;
constexpr float kSilentDb = -60.0f;
constexpr std::array<int, 5> kDenominatorValues{1, 2, 4, 8, 16};
const juce::StringArray kDenominators{"1", "2", "4", "8", "16"};

float gainOf(float db) noexcept
{
    return db <= kSilentDb ? 0.0f : juce::Decibels::decibelsToGain(db);
}

std::int64_t wrap(std::int64_t value, std::int64_t length) noexcept
{
    const auto r = value % length;
    return r < 0 ? r + length : r;
}

std::vector<float> overviewOf(const juce::AudioBuffer<float>& audio, std::int64_t length, int points)
{
    std::vector<float> peaks;
    if (length <= 0 || audio.getNumChannels() == 0)
        return peaks;
    peaks.resize(static_cast<std::size_t>(points), 0.0f);
    for (int p = 0; p < points; ++p)
    {
        const auto from = length * p / points;
        const auto to = juce::jmax(from + 1, length * (p + 1) / points);
        const auto count = static_cast<int>(juce::jmin(to, length) - from);
        float peak = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            peak = juce::jmax(peak, audio.getMagnitude(ch, static_cast<int>(from), count));
        peaks[static_cast<std::size_t>(p)] = juce::jmin(1.0f, peak);
    }
    return peaks;
}

// A file's audio as two channels at `rate` (the file's own when 0), at most
// maxFrames long. `usedRate` is the rate it came out at.
bool decode(juce::AudioFormatManager& formats,
            const juce::File& file,
            double rate,
            std::int64_t maxFrames,
            juce::AudioBuffer<float>& out,
            double& usedRate,
            juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "Couldn't read \"" + file.getFileName() + "\"";
        return false;
    }
    usedRate = rate > 0.0 ? rate : reader->sampleRate;
    const double ratio = reader->sampleRate / usedRate;
    const auto sourceLimit = static_cast<std::int64_t>(std::ceil(static_cast<double>(maxFrames) * ratio));
    const int length = static_cast<int>(juce::jmin<std::int64_t>(reader->lengthInSamples, sourceLimit));
    juce::AudioBuffer<float> source(2, length);
    source.clear();
    reader->read(&source, 0, length, 0, true, true); // a mono file fills both channels

    if (std::abs(reader->sampleRate - usedRate) < 0.5)
    {
        out = std::move(source);
        return true;
    }
    const int outLength = static_cast<int>(
        juce::jmin<std::int64_t>(maxFrames, static_cast<std::int64_t>(std::ceil(static_cast<double>(length) / ratio))));
    out.setSize(2, juce::jmax(1, outLength));
    out.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process(ratio, source.getReadPointer(ch), out.getWritePointer(ch), outLength, length, 0);
    }
    return true;
}
} // namespace

LooperProcessor::LooperProcessor(DiskThread& disk, double maxLoopSeconds)
    : ProcessorBase(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    "LooperProcessor"),
      disk_(disk), maxSeconds_(juce::jmax(0.01, maxLoopSeconds)),
      maxFrames_(static_cast<std::int64_t>(std::ceil(maxSeconds_ * kMaxRate)))
{
    addParameter(play_ = new juce::AudioParameterBool(juce::ParameterID{"play", 1}, "Play / Pause", false));
    addParameter(returnToZero_ =
                     new juce::AudioParameterBool(juce::ParameterID{"returnToZero", 1}, "Return to Zero", false));
    addParameter(record_ = new juce::AudioParameterBool(juce::ParameterID{"record", 1}, "Record", false));
    addParameter(sync_ = new juce::AudioParameterBool(juce::ParameterID{"sync", 1}, "Sync to Transport", false));
    addParameter(stopAfterBar_ =
                     new juce::AudioParameterBool(juce::ParameterID{"stopAfterBar", 1}, "Stop After Bar", false));
    addParameter(autoPlay_ = new juce::AudioParameterBool(juce::ParameterID{"autoPlay", 1}, "Auto-play", true));
    addParameter(numerator_ =
                     new juce::AudioParameterInt(juce::ParameterID{"numerator", 1}, "Beats per Bar", 1, 16, 4));
    addParameter(denominator_ = new juce::AudioParameterChoice(
                     juce::ParameterID{"denominator", 1}, "Note Value", kDenominators, 2));
    const juce::NormalisableRange<float> levels(kSilentDb, 6.0f);
    addParameter(inputLevel_ = new juce::AudioParameterFloat(juce::ParameterID{"inputLevel", 1},
                                                             "Input Level",
                                                             levels,
                                                             0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel("dB")));
    addParameter(loopLevel_ = new juce::AudioParameterFloat(juce::ParameterID{"loopLevel", 1},
                                                            "Loop Level",
                                                            levels,
                                                            0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel("dB")));

    limitFrames_.store(juce::jmin(maxFrames_, static_cast<std::int64_t>(std::llround(maxSeconds_ * 48000.0))));
    loop_ = std::make_unique<Loop>(maxFrames_);
    loop_->generation = nextGeneration_++;
    loop_->memory.setLimit(limitFrames_.load());
    loop_->memory.reserve(kHeadroomFrames); // recording can start at once

    for (auto* p : {play_, returnToZero_, record_})
        p->addListener(this);
    disk_.thread().addTimeSliceClient(this);
}

LooperProcessor::~LooperProcessor()
{
    // Detach from the disk thread (waiting out a slice in progress). takes_
    // then writes out a loop still recording and closes its take as it goes.
    disk_.thread().removeTimeSliceClient(this);
    for (auto* p : {play_, returnToZero_, record_})
        p->removeListener(this);
}

void LooperProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    deviceRate_.store(sampleRate_, std::memory_order_release);
    const auto limit = juce::jmin(maxFrames_, static_cast<std::int64_t>(std::llround(maxSeconds_ * sampleRate_)));
    limitFrames_.store(limit, std::memory_order_release);
    {
        const juce::SpinLock::ScopedLockType lock(swapLock_);
        loop_->memory.setLimit(limit);
    }
    takes_.setSampleRate(sampleRate_);

    // A loop from a file at another rate is decoded again at this one.
    const double loopRate = loopRate_.load(std::memory_order_acquire);
    if (loopRate > 0.0 && std::abs(loopRate - sampleRate_) > 0.5 && !recording_.load(std::memory_order_relaxed) &&
        fileValid_.load(std::memory_order_acquire))
        if (const auto file = getLoopFile(); file != juce::File())
            requestLoad(file, currentPreRoll(), originShared_.load(std::memory_order_relaxed));
}

bool LooperProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LooperProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin(LoopBuffer::kChannels, buffer.getNumChannels());
    if (numSamples <= 0 || channels == 0)
        return;

    const float inputGain = gainOf(inputLevel_->get());
    const float loopGain = gainOf(loopLevel_->get());
    if (firstBlock_)
    {
        firstBlock_ = false;
        lastInputGain_ = inputGain;
        lastLoopGain_ = loopGain;
    }

    // A new loop being swapped in: this block only passes the input through.
    const juce::SpinLock::ScopedTryLockType lock(swapLock_);
    Loop* loop = lock.isLocked() ? loop_.get() : nullptr;
    if (loop == nullptr)
    {
        buffer.applyGainRamp(0, numSamples, lastInputGain_, inputGain);
        lastInputGain_ = inputGain;
        return;
    }
    if (loop->generation != seenGeneration_)
        adopt(*loop);

    bool transportPlaying = false;
    std::int64_t transportSample = 0;
    double bpm = transport::kDefaultTempo;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            transportPlaying = position->getIsPlaying();
            transportSample = position->getTimeInSamples().orFallback(0);
            bpm = position->getBpm().orFallback(transport::kDefaultTempo);
        }
    const bool synced = sync_->get();

    // Record, Return to Zero and seeks, from the card, the window or a mapping.
    PreRoll fadeOut{};
    int fadeOutFrames = 0;
    int stopAt = -1;
    const auto requests = requests_.exchange(0, std::memory_order_acq_rel);
    if ((requests & kRecordRequest) != 0)
    {
        if (recordingNow_)
        {
            stopAt = 0;
        }
        else
        {
            // The loop that was playing fades out over the new one's first samples.
            fadeOutFrames = fadeOutInto(fadeOut, loop->memory, juce::jmin(numSamples, kFadeFrames));
            startRecording(transportSample, bpm);
        }
    }
    if ((requests & kReturnRequest) != 0)
        jumpTo(0, synced, transportSample);
    if (const auto seek = seekRequest_.exchange(-1, std::memory_order_acq_rel); seek >= 0)
        jumpTo(seek, synced, transportSample);

    // Recording: the input as it comes in, before its level.
    int playFrom = 0;
    if (recordingNow_ && stopAt < 0)
    {
        int wanted = numSamples;
        if (barFrames_ > 0)
            wanted = static_cast<int>(juce::jmin<std::int64_t>(numSamples, barFrames_ - written_));
        const int got = loop->memory.write(written_, buffer, 0, wanted);
        takes_.push(buffer, 0, got);
        written_ += got;
        writePos_.store(written_, std::memory_order_release);
        if (got < wanted)
        {
            ranOut_.store(true, std::memory_order_relaxed);
            stopAt = got;
        }
        else if (barFrames_ > 0 && written_ >= barFrames_)
        {
            stopAt = got;
        }
    }
    if (stopAt >= 0 && recordingNow_)
    {
        stopRecording();
        playFrom = stopAt;
    }

    // The last kFadeFrames of input, for the next recording's crossfade.
    for (int n = juce::jmax(0, numSamples - kFadeFrames); n < numSamples; ++n)
    {
        for (std::size_t ch = 0; ch < ring_.size(); ++ch)
            ring_[ch][ringPos_] = buffer.getSample(juce::jmin(static_cast<int>(ch), channels - 1), n);
        ringPos_ = (ringPos_ + 1) % static_cast<std::size_t>(kFadeFrames);
    }

    buffer.applyGainRamp(0, numSamples, lastInputGain_, inputGain);
    lastInputGain_ = inputGain;
    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < fadeOutFrames; ++n)
            buffer.addSample(ch, n, fadeOut[static_cast<std::size_t>(ch)][static_cast<std::size_t>(n)] * loopGain);

    const bool wantAudible =
        playing_.load(std::memory_order_relaxed) && lengthNow_ > 0 && !recordingNow_ && (!synced || transportPlaying);
    std::int64_t follow = -1; // synced: where the transport puts the loop at playFrom
    if (synced && transportPlaying && lengthNow_ > 0 && !recordingNow_)
        follow = wrap(transportSample + playFrom - origin_, lengthNow_);
    render(buffer, loop->memory, playFrom, wantAudible, follow, lastLoopGain_, loopGain);
    lastLoopGain_ = loopGain;
    shownPos_.store(pos_, std::memory_order_relaxed);
}

void LooperProcessor::adopt(const Loop& loop) noexcept
{
    if (recordingNow_)
    {
        takes_.endTake();
        recordingNow_ = false;
        recording_.store(false, std::memory_order_relaxed);
    }
    seenGeneration_ = loop.generation;
    lengthNow_ = loop.length;
    preRoll_ = loop.preRoll;
    origin_ = loop.origin;
    pos_ = 0;
    gain_ = 0.0f;
    pendingJump_ = -1;
    written_ = 0;
    writePos_.store(0, std::memory_order_release);
    // A synced loop follows the transport from the moment it's loaded.
    if (loop.length > 0 && sync_->get())
        playing_.store(true, std::memory_order_relaxed);
}

void LooperProcessor::startRecording(std::int64_t transportSample, double bpm) noexcept
{
    // The kFadeFrames of input just before the loop, oldest first: the loop's
    // end fades into them, so it runs on into its own start.
    for (std::size_t ch = 0; ch < preRoll_.size(); ++ch)
        for (std::size_t i = 0; i < static_cast<std::size_t>(kFadeFrames); ++i)
        {
            preRoll_[ch][i] = ring_[ch][(ringPos_ + i) % static_cast<std::size_t>(kFadeFrames)];
            preRollShared_[ch * static_cast<std::size_t>(kFadeFrames) + i].store(preRoll_[ch][i],
                                                                                 std::memory_order_relaxed);
        }

    barFrames_ = 0;
    if (stopAfterBar_->get() && bpm > 0.0)
    {
        const int denominator = kDenominatorValues[static_cast<std::size_t>(denominator_->getIndex())];
        const double quarterNotes = numerator_->get() * 4.0 / denominator;
        barFrames_ = juce::jmax<std::int64_t>(1, std::llround(quarterNotes * 60.0 / bpm * sampleRate_));
    }
    recordingNow_ = true;
    recording_.store(true, std::memory_order_relaxed);
    written_ = 0;
    writePos_.store(0, std::memory_order_release);
    ranOut_.store(false, std::memory_order_relaxed);
    lengthNow_ = 0;
    length_.store(0, std::memory_order_release);
    pos_ = 0;
    gain_ = 0.0f;
    pendingJump_ = -1;
    playing_.store(false, std::memory_order_relaxed);
    origin_ = transportSample;
    originShared_.store(origin_, std::memory_order_relaxed);
    fileValid_.store(false, std::memory_order_release);
    takes_.startTake();
}

void LooperProcessor::stopRecording() noexcept
{
    takes_.endTake();
    recordingNow_ = false;
    recording_.store(false, std::memory_order_relaxed);
    lengthNow_ = written_;
    length_.store(written_, std::memory_order_release);
    loopRate_.store(sampleRate_, std::memory_order_release);
    pos_ = 0;
    gain_ = 0.0f;
    pendingJump_ = -1;
    if (autoPlay_->get() && lengthNow_ > 0)
        playing_.store(true, std::memory_order_relaxed);
}

void LooperProcessor::jumpTo(std::int64_t frame, bool synced, std::int64_t transportSample) noexcept
{
    if (lengthNow_ <= 0 || recordingNow_)
        return;
    frame = wrap(frame, lengthNow_);
    if (synced)
    {
        // The transport puts the loop at `frame` now; render fades it there.
        origin_ = transportSample - frame;
        originShared_.store(origin_, std::memory_order_relaxed);
        if (gain_ <= 0.0f)
            pos_ = frame;
        return;
    }
    if (gain_ > 0.0f)
    {
        pendingJump_ = frame;
        jumpFollows_ = false;
    }
    else
    {
        pos_ = frame;
    }
}

int LooperProcessor::fadeOutInto(PreRoll& out, const LoopBuffer& memory, int frames) noexcept
{
    if (gain_ <= 0.0f || lengthNow_ <= 0 || frames <= 0)
        return 0;
    const float step = gain_ / static_cast<float>(frames);
    float gain = gain_;
    for (int n = 0; n < frames; ++n)
    {
        gain = juce::jmax(0.0f, gain - step);
        for (int ch = 0; ch < LoopBuffer::kChannels; ++ch)
            out[static_cast<std::size_t>(ch)][static_cast<std::size_t>(n)] = loopSample(memory, ch, pos_) * gain;
        if (++pos_ >= lengthNow_)
            pos_ = 0;
    }
    return frames;
}

void LooperProcessor::render(juce::AudioBuffer<float>& buffer,
                             const LoopBuffer& memory,
                             int from,
                             bool wantAudible,
                             std::int64_t follow,
                             float loopGainStart,
                             float loopGainEnd) noexcept
{
    if (lengthNow_ <= 0)
    {
        gain_ = 0.0f;
        pendingJump_ = -1;
        return;
    }

    // Synced: a loop that isn't where the transport puts it goes there, fading
    // out and back in when it's sounding.
    if (follow >= 0 && (pendingJump_ >= 0 || follow != pos_))
    {
        if (gain_ > 0.0f)
        {
            pendingJump_ = follow;
            jumpFollows_ = true;
        }
        else
        {
            pos_ = follow;
            pendingJump_ = -1;
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin(LoopBuffer::kChannels, buffer.getNumChannels());
    constexpr float step = 1.0f / static_cast<float>(kFadeFrames);
    const float loopGainStep = (loopGainEnd - loopGainStart) / static_cast<float>(numSamples);
    for (int n = from; n < numSamples; ++n)
    {
        if (pendingJump_ >= 0 && gain_ <= 0.0f)
        {
            pos_ = wrap(pendingJump_ + (jumpFollows_ ? n - from : 0), lengthNow_);
            pendingJump_ = -1;
        }
        const float target = wantAudible && pendingJump_ < 0 ? 1.0f : 0.0f;
        gain_ = gain_ < target ? juce::jmin(target, gain_ + step) : juce::jmax(target, gain_ - step);
        if (gain_ <= 0.0f)
            continue;
        const float gain = gain_ * (loopGainStart + loopGainStep * static_cast<float>(n + 1));
        for (int ch = 0; ch < channels; ++ch)
            buffer.addSample(ch, n, loopSample(memory, ch, pos_) * gain);
        if (++pos_ >= lengthNow_)
            pos_ = 0;
    }
}

float LooperProcessor::loopSample(const LoopBuffer& memory, int channel, std::int64_t position) const noexcept
{
    float value = memory.get(channel, position);
    const auto crossfade = juce::jmin<std::int64_t>(kFadeFrames, lengthNow_ / 2);
    const auto start = lengthNow_ - crossfade;
    if (crossfade > 0 && position >= start)
    {
        const auto i = position - start;
        const float w = static_cast<float>(i + 1) / static_cast<float>(crossfade);
        const float before =
            preRoll_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(kFadeFrames - crossfade + i)];
        value = value * (1.0f - w) + before * w;
    }
    return value;
}

void LooperProcessor::togglePlay() noexcept
{
    if (recording_.load(std::memory_order_relaxed))
    {
        requests_.fetch_or(kRecordRequest, std::memory_order_acq_rel);
        playing_.store(true, std::memory_order_relaxed);
        return;
    }
    if (length_.load(std::memory_order_acquire) <= 0)
        return;
    playing_.store(!playing_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void LooperProcessor::returnToZero() noexcept
{
    requests_.fetch_or(kReturnRequest, std::memory_order_acq_rel);
}

void LooperProcessor::toggleRecord() noexcept
{
    requests_.fetch_or(kRecordRequest, std::memory_order_acq_rel);
}

void LooperProcessor::seekTo(double fraction) noexcept
{
    const auto length = length_.load(std::memory_order_acquire);
    if (length > 0)
        seekRequest_.store(
            static_cast<std::int64_t>(juce::jlimit(0.0, 1.0, fraction) * static_cast<double>(length - 1)),
            std::memory_order_release);
}

double LooperProcessor::getLengthSeconds() const noexcept
{
    const double rate = deviceRate_.load(std::memory_order_relaxed);
    return static_cast<double>(length_.load(std::memory_order_relaxed)) / (rate > 0.0 ? rate : 48000.0);
}

double LooperProcessor::getPositionSeconds() const noexcept
{
    const double rate = deviceRate_.load(std::memory_order_relaxed);
    return static_cast<double>(shownPos_.load(std::memory_order_relaxed)) / (rate > 0.0 ? rate : 48000.0);
}

double LooperProcessor::getRecordedSeconds() const noexcept
{
    if (!recording_.load(std::memory_order_relaxed))
        return 0.0;
    const double rate = deviceRate_.load(std::memory_order_relaxed);
    return static_cast<double>(writePos_.load(std::memory_order_relaxed)) / (rate > 0.0 ? rate : 48000.0);
}

std::vector<float> LooperProcessor::getOverview() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return overview_;
}

juce::String LooperProcessor::getError() const
{
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        if (error_.isNotEmpty())
            return error_;
    }
    return takes_.getError();
}

void LooperProcessor::loadLoop(const juce::File& file)
{
    requestLoad(file, PreRoll{}, 0);
}

juce::File LooperProcessor::getLoopFile() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return loopFile_;
}

LooperProcessor::PreRoll LooperProcessor::currentPreRoll() const noexcept
{
    PreRoll preRoll{};
    for (std::size_t ch = 0; ch < preRoll.size(); ++ch)
        for (std::size_t i = 0; i < static_cast<std::size_t>(kFadeFrames); ++i)
            preRoll[ch][i] =
                preRollShared_[ch * static_cast<std::size_t>(kFadeFrames) + i].load(std::memory_order_relaxed);
    return preRoll;
}

void LooperProcessor::requestLoad(const juce::File& file, const PreRoll& preRoll, std::int64_t origin)
{
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        pendingFile_ = file;
        pendingPreRoll_ = preRoll;
        pendingOrigin_ = origin;
        loadRequested_ = true;
        error_.clear();
    }
    loading_.store(true, std::memory_order_release);
    disk_.thread().moveToFrontOfQueue(this);
}

void LooperProcessor::publishOverview(std::vector<float> overview)
{
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        overview_ = std::move(overview);
    }
    overviewGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

int LooperProcessor::useTimeSlice()
{
    takes_.pump();

    // Memory ahead of the recording (loop_ is only ever swapped on this thread).
    loop_->memory.reserve(writePos_.load(std::memory_order_acquire) + kHeadroomFrames);

    // A finished take is the loop's file now.
    if (const int count = takes_.getTakeCount(); count != takesSeen_)
    {
        takesSeen_ = count;
        const auto take = takes_.getLastTake();
        {
            const std::lock_guard<std::mutex> lk(fileMutex_);
            loopFile_ = take;
        }
        fileValid_.store(true, std::memory_order_release);
        juce::AudioBuffer<float> audio;
        double rate = 0.0;
        juce::String ignored;
        publishOverview(decode(disk_.formats(), take, 0.0, maxFrames_, audio, rate, ignored)
                            ? overviewOf(audio, audio.getNumSamples(), kOverviewPoints)
                            : std::vector<float>{});
    }

    bool requested = false;
    juce::File file;
    PreRoll preRoll{};
    std::int64_t origin = 0;
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        requested = loadRequested_;
        loadRequested_ = false;
        file = pendingFile_;
        preRoll = pendingPreRoll_;
        origin = pendingOrigin_;
    }
    if (requested)
    {
        load(file, preRoll, origin);
        return 0;
    }
    return recording_.load(std::memory_order_relaxed) || takes_.isTakeOpen() ? kBusySliceMs : kIdleSliceMs;
}

void LooperProcessor::load(const juce::File& file, const PreRoll& preRoll, std::int64_t origin)
{
    const double deviceRate = deviceRate_.load(std::memory_order_acquire);
    auto fresh = std::make_unique<Loop>(maxFrames_);
    fresh->memory.setLimit(limitFrames_.load(std::memory_order_acquire));
    fresh->preRoll = preRoll;
    fresh->origin = origin;

    juce::String error;
    std::vector<float> overview;
    double usedRate = deviceRate;
    if (file != juce::File())
    {
        juce::AudioBuffer<float> audio;
        if (decode(disk_.formats(), file, deviceRate, fresh->memory.limit(), audio, usedRate, error))
        {
            const int frames = audio.getNumSamples();
            fresh->memory.reserve(static_cast<std::int64_t>(frames) + kHeadroomFrames);
            fresh->length = fresh->memory.write(0, audio, 0, frames);
            overview = overviewOf(audio, fresh->length, kOverviewPoints);
        }
    }
    else
    {
        fresh->memory.reserve(kHeadroomFrames);
    }

    if (error.isNotEmpty())
    {
        {
            const std::lock_guard<std::mutex> lk(fileMutex_);
            error_ = error;
        }
        loading_.store(false, std::memory_order_release);
        return;
    }

    fresh->generation = nextGeneration_++;
    const auto length = fresh->length;
    {
        const juce::SpinLock::ScopedLockType lock(swapLock_);
        std::swap(loop_, fresh);
    }
    fresh.reset(); // the previous loop, freed here rather than on the audio thread

    length_.store(length, std::memory_order_release);
    loopRate_.store(usedRate, std::memory_order_release);
    originShared_.store(origin, std::memory_order_relaxed);
    for (std::size_t ch = 0; ch < preRoll.size(); ++ch)
        for (std::size_t i = 0; i < static_cast<std::size_t>(kFadeFrames); ++i)
            preRollShared_[ch * static_cast<std::size_t>(kFadeFrames) + i].store(preRoll[ch][i],
                                                                                 std::memory_order_relaxed);
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        loopFile_ = file;
    }
    fileValid_.store(file != juce::File(), std::memory_order_release);
    publishOverview(std::move(overview));
    loading_.store(false, std::memory_order_release);

    // The device changed rate while this was decoding: again, at the new one.
    if (const double now = deviceRate_.load(std::memory_order_acquire);
        file != juce::File() && now > 0.0 && std::abs(now - usedRate) > 0.5)
        requestLoad(file, preRoll, origin);
}

void LooperProcessor::writeState(juce::XmlElement& xml) const
{
    if (const auto folder = takes_.getChosenFolder(); folder != juce::File())
        xml.setAttribute("folder", folder.getFullPathName());
    if (fileValid_.load(std::memory_order_acquire))
        if (const auto file = getLoopFile(); file != juce::File())
            xml.setAttribute("loopFile", file.getFullPathName());
    const PreRoll preRoll = currentPreRoll();
    xml.setAttribute("preRoll", juce::MemoryBlock(&preRoll, sizeof(PreRoll)).toBase64Encoding());
    xml.setAttribute("origin", juce::String(static_cast<juce::int64>(originShared_.load(std::memory_order_relaxed))));
}

void LooperProcessor::readState(const juce::XmlElement& xml)
{
    const auto folder = xml.getStringAttribute("folder");
    takes_.setFolder(juce::File::isAbsolutePath(folder) ? juce::File(folder) : juce::File());

    PreRoll preRoll{};
    juce::MemoryBlock block;
    if (block.fromBase64Encoding(xml.getStringAttribute("preRoll")) && block.getSize() == sizeof(PreRoll))
        std::memcpy(&preRoll, block.getData(), sizeof(PreRoll));
    const auto origin = static_cast<std::int64_t>(xml.getStringAttribute("origin").getLargeIntValue());

    const auto path = xml.getStringAttribute("loopFile");
    if (!juce::File::isAbsolutePath(path))
        return;
    if (const juce::File file(path); file.existsAsFile())
    {
        requestLoad(file, preRoll, origin);
    }
    else
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        error_ = "The loop's file is missing: " + file.getFullPathName();
    }
}

void LooperProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    // A mapped footswitch: the press acts, the release does nothing.
    if (newValue < 0.5f)
        return;
    if (parameterIndex == play_->getParameterIndex())
        togglePlay();
    else if (parameterIndex == returnToZero_->getParameterIndex())
        returnToZero();
    else if (parameterIndex == record_->getParameterIndex())
        toggleRecord();
}

} // namespace pedalboard3::engine::processors
