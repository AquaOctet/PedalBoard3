#include "AudioSettingsPanel.h"

#include "ColourScheme.h"
#include "DialogFrame.h"
#include "Fonts.h"
#include "Pb3LookAndFeel.h"

#include <cmath>

namespace pedalboard3::ui
{

namespace
{

constexpr int kWidth = 560;
constexpr int kHeight = 510;
constexpr int kPad = 14;
constexpr int kGap = 12;
constexpr int kCaptionHeight = 18;
constexpr int kFieldHeight = 28;
constexpr int kChipWidth = 44;
constexpr int kChipGap = 6;
constexpr int kMeterHeight = 6;
constexpr int kLatencyHeight = 40;
constexpr int kErrorHeight = 36;
constexpr int kButtonHeight = 34;
constexpr int kNoInputId = 1; // the input field's "None"
constexpr int kMeterHz = 20;

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text.toUpperCase(), juce::dontSendNotification);
    label.setFont(fonts::caption(12.0f));
    label.setColour(juce::Label::textColourId, tokens::fgMuted);
    label.setBorderSize({});
}

// The names as a field's items, ids from 1, with `selected` chosen.
void fill(juce::ComboBox& box, const juce::StringArray& names, const juce::String& selected)
{
    box.clear(juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        box.addItem(names[i], i + 1);
    if (const int index = names.indexOf(selected); index >= 0)
        box.setSelectedId(index + 1, juce::dontSendNotification);
}

// Chips in rows from the top left of `area`; returns the height they take.
int layoutChips(std::vector<std::unique_ptr<juce::TextButton>>& chips, juce::Rectangle<int> area)
{
    int x = area.getX();
    int y = area.getY();
    for (auto& chip : chips)
    {
        if (x + kChipWidth > area.getRight() && x > area.getX())
        {
            x = area.getX();
            y += kFieldHeight + kChipGap;
        }
        chip->setBounds(x, y, kChipWidth, kFieldHeight);
        x += kChipWidth + kChipGap;
    }
    return chips.empty() ? 0 : y + kFieldHeight - area.getY();
}

// A level as the meter's fill: -60 dB to 0 dB across.
float meterProportion(float gain)
{
    const auto decibels = juce::Decibels::gainToDecibels(gain, -60.0f);
    return juce::jlimit(0.0f, 1.0f, (decibels + 60.0f) / 60.0f);
}

} // namespace

AudioSettingsPanel::AudioSettingsPanel(juce::AudioDeviceManager& devices, juce::Value& testTone)
    : devices_(devices), inputLevel_(devices.getInputLevelGetter())
{
    setOpaque(true);

    styleCaption(driverCaption_, "Driver");
    styleCaption(outputCaption_, "Output");
    styleCaption(inputCaption_, "Input");
    styleCaption(formatCaption_, "Format");
    styleCaption(toneCaption_, "Test Tone");
    for (auto* caption : {&driverCaption_, &outputCaption_, &inputCaption_, &formatCaption_, &toneCaption_})
        addAndMakeVisible(caption);

    driverBox_.onChange = [this]
    {
        if (!refreshing_)
            devices_.setCurrentAudioDeviceType(driverBox_.getText(), true);
    };
    outputBox_.onChange = [this]
    {
        if (refreshing_)
            return;
        auto setup = devices_.getAudioDeviceSetup();
        setup.outputDeviceName = outputBox_.getText();
        if (auto* type = devices_.getCurrentDeviceTypeObject(); type != nullptr && !type->hasSeparateInputsAndOutputs())
            setup.inputDeviceName = setup.outputDeviceName; // ASIO: one device both ways
        apply(setup);
    };
    inputBox_.onChange = [this]
    {
        if (refreshing_)
            return;
        auto setup = devices_.getAudioDeviceSetup();
        setup.inputDeviceName = inputBox_.getSelectedId() == kNoInputId ? juce::String() : inputBox_.getText();
        apply(setup);
    };
    rateBox_.onChange = [this]
    {
        const int index = rateBox_.getSelectedItemIndex();
        if (refreshing_ || !juce::isPositiveAndBelow(index, rates_.size()))
            return;
        auto setup = devices_.getAudioDeviceSetup();
        setup.sampleRate = rates_[index];
        apply(setup);
    };
    bufferBox_.onChange = [this]
    {
        const int index = bufferBox_.getSelectedItemIndex();
        if (refreshing_ || !juce::isPositiveAndBelow(index, buffers_.size()))
            return;
        auto setup = devices_.getAudioDeviceSetup();
        setup.bufferSize = buffers_[index];
        apply(setup);
    };
    for (auto* box : {&driverBox_, &outputBox_, &inputBox_, &rateBox_, &bufferBox_})
        addAndMakeVisible(box);

    controlPanel_.setTooltip("Open the driver's own settings");
    controlPanel_.onClick = [this]
    {
        // As JUCE's device selector does: reopen the device after the driver's
        // panel may have changed it.
        if (auto* device = devices_.getCurrentAudioDevice();
            device != nullptr && device->hasControlPanel() && device->showControlPanel())
        {
            devices_.closeAudioDevice();
            devices_.restartLastAudioDevice();
        }
    };
    addChildComponent(controlPanel_);

    error_.setFont(fonts::ui(13.0f));
    error_.setColour(juce::Label::textColourId, tokens::accentDestructive);
    error_.setJustificationType(juce::Justification::topLeft);
    error_.setBorderSize({});
    addAndMakeVisible(error_);

    toneButton_.getToggleStateValue().referTo(testTone); // shared with the Settings menu
    toneButton_.setTooltip("Play a 440 Hz tone straight to the output, bypassing the chain");
    toneHint_.setText("Checks the output device without the board. Switching patch turns it off.",
                      juce::dontSendNotification);
    toneHint_.setFont(fonts::ui(12.0f));
    toneHint_.setColour(juce::Label::textColourId, tokens::fgMuted);
    toneHint_.setBorderSize({});
    addAndMakeVisible(toneButton_);
    addAndMakeVisible(toneHint_);

    closeButton_.getProperties().set(Pb3LookAndFeel::kOutlineButton, true);
    closeButton_.onClick = [this]
    {
        if (auto* frame = findParentComponentOfClass<DialogFrame>())
            frame->closeButtonPressed();
    };
    addAndMakeVisible(closeButton_);

    devices_.addChangeListener(this);
    refresh();
    startTimerHz(kMeterHz);
    setSize(kWidth, kHeight);
}

AudioSettingsPanel::~AudioSettingsPanel()
{
    stopTimer();
    devices_.removeChangeListener(this);
}

void AudioSettingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void AudioSettingsPanel::timerCallback()
{
    const auto level = inputLevel_ != nullptr ? static_cast<float>(inputLevel_->getCurrentLevel()) : 0.0f;
    if (std::abs(level - shownLevel_) > 0.001f)
    {
        shownLevel_ = level;
        repaint(meterArea_);
    }
}

void AudioSettingsPanel::refresh()
{
    const juce::ScopedValueSetter<bool> quiet(refreshing_, true);
    auto* type = devices_.getCurrentDeviceTypeObject();
    auto* device = devices_.getCurrentAudioDevice();
    const auto setup = devices_.getAudioDeviceSetup();

    driverBox_.clear(juce::dontSendNotification);
    int typeId = 1;
    for (auto* available : devices_.getAvailableDeviceTypes())
    {
        driverBox_.addItem(available->getTypeName(), typeId);
        if (available == type)
            driverBox_.setSelectedId(typeId, juce::dontSendNotification);
        ++typeId;
    }

    fill(outputBox_, type != nullptr ? type->getDeviceNames(false) : juce::StringArray(), setup.outputDeviceName);

    inputBox_.clear(juce::dontSendNotification);
    inputBox_.addItem("None", kNoInputId);
    const auto inputs = type != nullptr ? type->getDeviceNames(true) : juce::StringArray();
    for (int i = 0; i < inputs.size(); ++i)
        inputBox_.addItem(inputs[i], kNoInputId + 1 + i);
    const int inputIndex = inputs.indexOf(setup.inputDeviceName);
    inputBox_.setSelectedId(inputIndex >= 0 ? kNoInputId + 1 + inputIndex : kNoInputId, juce::dontSendNotification);
    // ASIO opens one device both ways: the input follows the output.
    inputBox_.setEnabled(type != nullptr && type->hasSeparateInputsAndOutputs());

    const double rate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    const int buffer = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;

    rates_ = device != nullptr ? device->getAvailableSampleRates() : juce::Array<double>();
    rateBox_.clear(juce::dontSendNotification);
    for (int i = 0; i < rates_.size(); ++i)
        rateBox_.addItem(audiosettings::rateText(rates_[i]), i + 1);
    if (const int index = rates_.indexOf(rate); index >= 0)
        rateBox_.setSelectedId(index + 1, juce::dontSendNotification);

    buffers_ = audiosettings::bufferChoices(device != nullptr ? device->getAvailableBufferSizes() : juce::Array<int>(),
                                            buffer);
    bufferBox_.clear(juce::dontSendNotification);
    for (int i = 0; i < buffers_.size(); ++i)
        bufferBox_.addItem(audiosettings::bufferText(buffers_[i], rate), i + 1);
    if (const int index = buffers_.indexOf(buffer); index >= 0)
        bufferBox_.setSelectedId(index + 1, juce::dontSendNotification);

    rateBox_.setEnabled(device != nullptr);
    bufferBox_.setEnabled(device != nullptr);

    rebuildChips(outputChips_,
                 device != nullptr ? device->getOutputChannelNames() : juce::StringArray(),
                 device != nullptr ? device->getActiveOutputChannels() : juce::BigInteger(),
                 false);
    rebuildChips(inputChips_,
                 device != nullptr ? device->getInputChannelNames() : juce::StringArray(),
                 device != nullptr ? device->getActiveInputChannels() : juce::BigInteger(),
                 true);

    controlPanel_.setVisible(device != nullptr && device->hasControlPanel());

    outputLatency_ =
        device != nullptr
            ? audiosettings::latencyText(
                  audiosettings::directionLatencySamples(device->getOutputLatencyInSamples(), buffer), rate)
            : juce::String();
    const bool anyInput = device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0;
    inputLatency_ = anyInput
                        ? audiosettings::latencyText(
                              audiosettings::directionLatencySamples(device->getInputLatencyInSamples(), buffer), rate)
                        : juce::String();

    if (device == nullptr && error_.getText().isEmpty())
        error_.setText("No audio device is open. Choose one above.", juce::dontSendNotification);

    resized();
    repaint();
}

void AudioSettingsPanel::rebuildChips(std::vector<std::unique_ptr<juce::TextButton>>& chips,
                                      const juce::StringArray& channelNames,
                                      const juce::BigInteger& active,
                                      bool input)
{
    chips.clear();
    for (const auto& pair : audiosettings::channelPairs(channelNames.size()))
    {
        auto chip = std::make_unique<juce::TextButton>(pair.label);
        chip->setToggleState(audiosettings::isPairOn(active, pair), juce::dontSendNotification);
        chip->setTooltip(channelNames[pair.first] +
                         (pair.count == 2 ? " + " + channelNames[pair.first + 1] : juce::String()));
        chip->onClick = [this, pair, input] { toggleChannels(pair, input); };
        addAndMakeVisible(*chip);
        chips.push_back(std::move(chip));
    }
}

void AudioSettingsPanel::toggleChannels(const audiosettings::ChannelPair& pair, bool input)
{
    auto* device = devices_.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    auto setup = devices_.getAudioDeviceSetup();
    if (input)
    {
        setup.inputChannels = audiosettings::togglePair(device->getActiveInputChannels(), pair);
        setup.useDefaultInputChannels = false;
    }
    else
    {
        setup.outputChannels = audiosettings::togglePair(device->getActiveOutputChannels(), pair);
        setup.useDefaultOutputChannels = false;
    }
    apply(setup);
}

void AudioSettingsPanel::apply(const juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    const auto error = devices_.setAudioDeviceSetup(setup, true);
    error_.setText(error, juce::dontSendNotification);

    // Refresh after this click has finished (a chip can't delete itself), and
    // even when the device manager doesn't broadcast a failed change.
    juce::Component::SafePointer<AudioSettingsPanel> safeThis(this);
    juce::Timer::callAfterDelay(1,
                                [safeThis]
                                {
                                    if (safeThis != nullptr)
                                        safeThis->refresh();
                                });
}

void AudioSettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);

    if (!meterArea_.isEmpty())
    {
        const auto meter = meterArea_.toFloat();
        g.setColour(tokens::bgSecondary);
        g.fillRoundedRectangle(meter, 3.0f);
        g.setColour(tokens::accentPrimary);
        g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * meterProportion(shownLevel_)), 3.0f);
    }

    if (!latencyArea_.isEmpty())
    {
        g.setColour(tokens::bgSecondary.withAlpha(0.5f));
        g.fillRoundedRectangle(latencyArea_.toFloat(), 6.0f);
        const auto area = latencyArea_.reduced(12, 0);
        g.setFont(fonts::caption(12.0f));
        g.setColour(tokens::fgMuted);
        g.drawText("LATENCY", area, juce::Justification::centredLeft);

        juce::String text;
        if (outputLatency_.isNotEmpty())
            text << "Out " << outputLatency_;
        if (inputLatency_.isNotEmpty())
            text << (text.isEmpty() ? "" : "   ") << "In " << inputLatency_;
        g.setFont(fonts::mono(13.0f));
        g.setColour(tokens::accentPrimary);
        g.drawText(text.isEmpty() ? juce::String("--") : text, area, juce::Justification::centredRight);
    }
}

void AudioSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(kPad);

    driverCaption_.setBounds(area.removeFromTop(kCaptionHeight));
    driverBox_.setBounds(area.removeFromTop(kFieldHeight));
    area.removeFromTop(kGap);

    // Output and input side by side: caption, device, chips (up to two rows),
    // then Control Panel or the input meter.
    auto columns =
        area.removeFromTop(kCaptionHeight + kFieldHeight + 8 + 2 * kFieldHeight + kChipGap + 8 + kFieldHeight);
    const int columnWidth = (columns.getWidth() - kGap) / 2;
    auto output = columns.removeFromLeft(columnWidth);
    columns.removeFromLeft(kGap);
    auto input = columns;

    outputCaption_.setBounds(output.removeFromTop(kCaptionHeight));
    outputBox_.setBounds(output.removeFromTop(kFieldHeight));
    output.removeFromTop(8);
    output.removeFromTop(layoutChips(outputChips_, output) + 8);
    controlPanel_.setBounds(output.removeFromTop(kFieldHeight).removeFromLeft(140));

    inputCaption_.setBounds(input.removeFromTop(kCaptionHeight));
    inputBox_.setBounds(input.removeFromTop(kFieldHeight));
    input.removeFromTop(8);
    input.removeFromTop(layoutChips(inputChips_, input) + 8);
    meterArea_ = input.removeFromTop(kFieldHeight).withSizeKeepingCentre(input.getWidth(), kMeterHeight);

    area.removeFromTop(kGap);
    formatCaption_.setBounds(area.removeFromTop(kCaptionHeight));
    auto format = area.removeFromTop(kFieldHeight);
    rateBox_.setBounds(format.removeFromLeft(columnWidth));
    format.removeFromLeft(kGap);
    bufferBox_.setBounds(format);
    area.removeFromTop(8);
    latencyArea_ = area.removeFromTop(kLatencyHeight);
    area.removeFromTop(8);
    error_.setBounds(area.removeFromTop(kErrorHeight));

    auto footer = area.removeFromBottom(kButtonHeight);
    closeButton_.setBounds(footer.removeFromRight(96));
    toneCaption_.setBounds(area.removeFromTop(kCaptionHeight));
    toneButton_.setBounds(area.removeFromTop(kFieldHeight).removeFromLeft(200));
    area.removeFromTop(4);
    toneHint_.setBounds(area.removeFromTop(18));
}

} // namespace pedalboard3::ui
