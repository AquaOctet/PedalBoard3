#include "MetronomeProcessor.h"

#include "engine/DiskThread.h"
#include "engine/TransportMath.h"

#include <cmath>
#include <optional>

namespace pedalboard3::engine::processors
{

namespace
{
constexpr int kIdleSliceMs = 250;
const juce::StringArray kDenominators{"1", "2", "4", "8", "16"};

// A file's audio as one channel at `rate`, at most kMaxSampleSeconds long.
juce::AudioBuffer<float> decodeMono(juce::AudioFormatManager& formats,
                                    const juce::File& file,
                                    double rate,
                                    juce::String& error)
{
    juce::AudioBuffer<float> mono;
    if (file == juce::File())
        return mono;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
    {
        error = "Couldn't read \"" + file.getFileName() + "\"";
        return mono;
    }
    const int length = static_cast<int>(juce::jmin<std::int64_t>(
        reader->lengthInSamples,
        static_cast<std::int64_t>(reader->sampleRate * MetronomeProcessor::kMaxSampleSeconds)));
    juce::AudioBuffer<float> source(1, juce::jmax(1, length));
    source.clear();
    reader->read(&source, 0, length, 0, true, false);

    const double target = rate > 0.0 ? rate : reader->sampleRate;
    if (std::abs(reader->sampleRate - target) < 0.5)
        return source;
    const double ratio = reader->sampleRate / target;
    const int outLength = static_cast<int>(std::ceil(length / ratio));
    mono.setSize(1, juce::jmax(1, outLength));
    juce::LagrangeInterpolator interpolator;
    interpolator.process(ratio, source.getReadPointer(0), mono.getWritePointer(0), outLength, length, 0);
    return mono;
}
} // namespace

MetronomeProcessor::MetronomeProcessor(DiskThread& disk)
    : ProcessorBase(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    "MetronomeProcessor"),
      disk_(disk)
{
    addParameter(play_ = new juce::AudioParameterBool(juce::ParameterID{"play", 1}, "Play", false));
    addParameter(sync_ = new juce::AudioParameterBool(juce::ParameterID{"sync", 1}, "Sync to Transport", false));
    addParameter(numerator_ =
                     new juce::AudioParameterInt(juce::ParameterID{"numerator", 1}, "Beats per Bar", 1, 16, 4));
    addParameter(denominator_ = new juce::AudioParameterChoice(
                     juce::ParameterID{"denominator", 1}, "Note Value", kDenominators, 2));
    addParameter(level_ = new juce::AudioParameterFloat(juce::ParameterID{"level", 1},
                                                        "Level",
                                                        juce::NormalisableRange<float>(-40.0f, 6.0f),
                                                        -6.0f,
                                                        juce::AudioParameterFloatAttributes().withLabel("dB")));
    play_->addListener(this);
    disk_.thread().addTimeSliceClient(this);
}

MetronomeProcessor::~MetronomeProcessor()
{
    disk_.thread().removeTimeSliceClient(this);
    play_->removeListener(this);
}

void MetronomeProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    clickLength_ = juce::jmax(1, static_cast<int>(std::lround(kClickSeconds * sampleRate_)));
    const double previous = deviceRate_.exchange(sampleRate_, std::memory_order_acq_rel);
    if (std::abs(previous - sampleRate_) > 0.5 && (getAccentFile() != juce::File() || getClickFile() != juce::File()))
        setSampleFiles(getAccentFile(), getClickFile()); // decode again at the new rate
}

bool MetronomeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

int MetronomeProcessor::findBeats(int numSamples, std::array<Beat, 32>& beats) noexcept
{
    int count = 0;
    const auto add = [&beats, &count](double offset, bool accent)
    {
        if (count < static_cast<int>(beats.size()))
            beats[static_cast<std::size_t>(count++)] = {static_cast<int>(std::llround(offset)), accent};
    };

    const bool synced = sync_->get();
    std::optional<juce::AudioPlayHead::PositionInfo> position;
    if (auto* head = getPlayHead())
        if (auto info = head->getPosition())
            position = *info;
    const double bpm =
        position.has_value() ? position->getBpm().orFallback(transport::kDefaultTempo) : transport::kDefaultTempo;

    if (synced)
    {
        const bool playing = position.has_value() && position->getIsPlaying();
        running_.store(playing, std::memory_order_relaxed);
        if (!playing)
        {
            wasTransportPlaying_ = false;
            beatInBar_.store(-1, std::memory_order_relaxed);
            return 0;
        }
        const auto signature = position->getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature{});
        const int beatsPerBar = juce::jmax(1, signature.numerator);
        const double beatLength = 4.0 / juce::jmax(1, signature.denominator); // in quarter notes
        beatsPerBar_.store(beatsPerBar, std::memory_order_relaxed);
        const double ppq = position->getPpqPosition().orFallback(0.0);
        const double samplesPerPpq = sampleRate_ * 60.0 / bpm;

        // Starting, a jump back (Return to Zero) or a new beat length: pick up
        // from the transport's position. Otherwise carry on counting.
        if (!wasTransportPlaying_ || !lastSynced_ || ppq < lastPpqEnd_ - 1e-6 ||
            !juce::approximatelyEqual(beatLength, lastBeatLength_))
            nextBeatIndex_ = static_cast<std::int64_t>(std::ceil(ppq / beatLength - 1e-9));
        wasTransportPlaying_ = true;
        lastBeatLength_ = beatLength;

        for (;;)
        {
            const double offset = (static_cast<double>(nextBeatIndex_) * beatLength - ppq) * samplesPerPpq;
            if (offset >= numSamples - 0.5)
                break;
            const auto inBar = static_cast<int>(nextBeatIndex_ % beatsPerBar);
            add(juce::jmax(0.0, offset), inBar == 0);
            beatInBar_.store(inBar, std::memory_order_relaxed);
            ++nextBeatIndex_;
        }
        lastPpqEnd_ = ppq + numSamples / samplesPerPpq;
        return count;
    }

    // Unsynced: its own count, from Play, at the patch tempo.
    wasTransportPlaying_ = false;
    if (restartOwn_.exchange(false, std::memory_order_acq_rel))
    {
        ownSamples_ = 0;
        ownNextBeat_ = 0.0;
        ownBeat_ = 0;
    }
    const bool running = ownRunning_.load(std::memory_order_relaxed);
    running_.store(running, std::memory_order_relaxed);
    const int beatsPerBar = numerator_->get();
    beatsPerBar_.store(beatsPerBar, std::memory_order_relaxed);
    if (!running)
    {
        beatInBar_.store(-1, std::memory_order_relaxed);
        return 0;
    }
    const int denominator = kDenominators[denominator_->getIndex()].getIntValue();
    const double beatSamples = sampleRate_ * 60.0 / bpm * 4.0 / denominator;
    while (ownNextBeat_ < static_cast<double>(ownSamples_ + numSamples) - 0.5)
    {
        const auto inBar = static_cast<int>(ownBeat_ % beatsPerBar);
        add(ownNextBeat_ - static_cast<double>(ownSamples_), inBar == 0);
        beatInBar_.store(inBar, std::memory_order_relaxed);
        ++ownBeat_;
        ownNextBeat_ += beatSamples;
    }
    ownSamples_ += numSamples;
    return count;
}

float MetronomeProcessor::voiceSample(Voice& voice, const Samples* samples) noexcept
{
    if (voice.age < 0)
        return 0.0f;
    float value = 0.0f;
    const juce::AudioBuffer<float>* sample =
        samples == nullptr ? nullptr : (voice.accent ? &samples->accent : &samples->click);
    if (sample != nullptr && sample->getNumSamples() > 1)
    {
        if (voice.age < sample->getNumSamples())
            value = sample->getSample(0, voice.age);
        else
            voice.age = -2; // done
    }
    else if (voice.age < clickLength_)
    {
        const double hz = voice.accent ? kAccentHz : kClickHz;
        const double envelope = 1.0 - static_cast<double>(voice.age) / clickLength_;
        value =
            static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * hz * voice.age / sampleRate_) * envelope);
    }
    else
    {
        voice.age = -2;
    }
    ++voice.age; // -2 becomes -1: silent
    return value;
}

void MetronomeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || buffer.getNumChannels() == 0)
        return;

    std::array<Beat, 32> beats{};
    const int beatCount = findBeats(numSamples, beats);
    lastSynced_ = sync_->get();

    // A new sample set is picked up without waiting; if one is being swapped in
    // right now, this block uses the synthesized click.
    const juce::SpinLock::ScopedTryLockType lock(swapLock_);
    const Samples* samples = lock.isLocked() ? samples_.get() : nullptr;

    const float gain = juce::Decibels::decibelsToGain(level_->get());
    int next = 0;
    for (int n = 0; n < numSamples; ++n)
    {
        while (next < beatCount && beats[static_cast<std::size_t>(next)].offset <= n)
        {
            voices_[nextVoice_] = {0, beats[static_cast<std::size_t>(next)].accent};
            nextVoice_ = (nextVoice_ + 1) % voices_.size();
            ++next;
        }
        float value = 0.0f;
        for (auto& voice : voices_)
            value += voiceSample(voice, samples);
        if (value == 0.0f)
            continue;
        value *= gain;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample(ch, n, value);
    }
}

void MetronomeProcessor::togglePlay() noexcept
{
    const bool start = !ownRunning_.load(std::memory_order_relaxed);
    if (start)
        restartOwn_.store(true, std::memory_order_release); // from beat 1
    ownRunning_.store(start, std::memory_order_relaxed);
}

void MetronomeProcessor::setSampleFiles(const juce::File& accent, const juce::File& click)
{
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        accentFile_ = accent;
        clickFile_ = click;
        loadRequested_ = true;
        error_.clear();
    }
    loading_.store(true, std::memory_order_release);
    disk_.thread().moveToFrontOfQueue(this);
}

juce::File MetronomeProcessor::getAccentFile() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return accentFile_;
}

juce::File MetronomeProcessor::getClickFile() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return clickFile_;
}

juce::String MetronomeProcessor::getLoadError() const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    return error_;
}

void MetronomeProcessor::writeState(juce::XmlElement& xml) const
{
    const std::lock_guard<std::mutex> lk(fileMutex_);
    if (accentFile_ != juce::File())
        xml.setAttribute("accentFile", accentFile_.getFullPathName());
    if (clickFile_ != juce::File())
        xml.setAttribute("clickFile", clickFile_.getFullPathName());
}

void MetronomeProcessor::readState(const juce::XmlElement& xml)
{
    const auto fileAt = [&xml](const char* key)
    {
        const auto path = xml.getStringAttribute(key);
        return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
    };
    const auto accent = fileAt("accentFile");
    const auto click = fileAt("clickFile");
    if (accent != juce::File() || click != juce::File())
        setSampleFiles(accent, click);
}

void MetronomeProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    // A mapped footswitch: the press starts or stops it, the release does nothing.
    if (parameterIndex == play_->getParameterIndex() && newValue >= 0.5f)
        togglePlay();
}

int MetronomeProcessor::useTimeSlice()
{
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        if (!loadRequested_)
            return kIdleSliceMs;
        loadRequested_ = false;
    }
    load();
    return 0;
}

void MetronomeProcessor::load()
{
    juce::File accent, click;
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        accent = accentFile_;
        click = clickFile_;
    }
    const double rate = deviceRate_.load(std::memory_order_acquire);
    juce::String error;
    std::unique_ptr<Samples> fresh;
    if (accent != juce::File() || click != juce::File())
    {
        fresh = std::make_unique<Samples>();
        fresh->accent = decodeMono(disk_.formats(), accent, rate, error);
        fresh->click = decodeMono(disk_.formats(), click, rate, error);
    }
    {
        const juce::SpinLock::ScopedLockType lock(swapLock_);
        std::swap(samples_, fresh);
    }
    fresh.reset(); // the previous samples, freed off the audio thread
    {
        const std::lock_guard<std::mutex> lk(fileMutex_);
        error_ = error;
    }
    loading_.store(false, std::memory_order_release);
}

} // namespace pedalboard3::engine::processors
