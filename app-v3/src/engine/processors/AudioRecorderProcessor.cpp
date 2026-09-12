#include "AudioRecorderProcessor.h"

#include "engine/DiskThread.h"

namespace pedalboard3::engine::processors
{

namespace
{
constexpr int kBusySliceMs = 10;
constexpr int kIdleSliceMs = 100;
} // namespace

AudioRecorderProcessor::AudioRecorderProcessor(DiskThread& disk)
    : ProcessorBase(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    "AudioRecorderProcessor"),
      disk_(disk)
{
    addParameter(record_ = new juce::AudioParameterBool(juce::ParameterID{"record", 1}, "Record", false));
    addParameter(sync_ = new juce::AudioParameterBool(juce::ParameterID{"sync", 1}, "Sync to Transport", false));
    record_->addListener(this);
    disk_.thread().addTimeSliceClient(this);
}

AudioRecorderProcessor::~AudioRecorderProcessor()
{
    // Keep what was recorded: detach from the disk thread (waiting out a slice
    // in progress). takes_ then writes out what's left and closes the take as
    // it goes.
    wantRecording_.store(false, std::memory_order_relaxed);
    disk_.thread().removeTimeSliceClient(this);
    record_->removeListener(this);
}

void AudioRecorderProcessor::prepareToPlay(double sampleRate, int)
{
    if (sampleRate > 0.0)
    {
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        takes_.setSampleRate(sampleRate);
    }
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
        takes_.endTake();
    }
    if (want && !capturing_.load(std::memory_order_relaxed))
    {
        capturing_.store(true, std::memory_order_relaxed);
        takeFrames_.store(0, std::memory_order_relaxed);
        takes_.startTake();
    }
    if (!capturing_.load(std::memory_order_relaxed))
        return;

    if (takes_.push(buffer, 0, numSamples))
        takeFrames_.fetch_add(numSamples, std::memory_order_relaxed);
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

void AudioRecorderProcessor::writeState(juce::XmlElement& xml) const
{
    if (const auto folder = takes_.getChosenFolder(); folder != juce::File())
        xml.setAttribute("folder", folder.getFullPathName());
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
    takes_.pump();
    return takes_.isTakeOpen() || capturing_.load(std::memory_order_relaxed) ? kBusySliceMs : kIdleSliceMs;
}

} // namespace pedalboard3::engine::processors
