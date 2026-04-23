#include "MainWindow.h"

#include "AppContext.h"
#include "engine/AudioEngine.h"
#include "engine/AudioGraphRuntime.h"
#include "engine/PluginHost.h"
#include "engine/processors/TestToneProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

namespace pedalboard3::ui
{

namespace
{

// Build a minimal graph that routes a TestToneProcessor straight to the
// master output. Used by the "Test Tone" toggle so the user can verify
// end-to-end audio without any plugin hosted. Prepared to whatever sample
// rate / buffer size the current audio device is running at, or 48k/512
// as a fallback when no device is open yet.
std::unique_ptr<juce::AudioProcessorGraph> buildTestToneGraph(juce::AudioDeviceManager& dm)
{
    double sr = 48000.0;
    int bs = 512;
    if (auto* dev = dm.getCurrentAudioDevice())
    {
        sr = dev->getCurrentSampleRate();
        bs = dev->getCurrentBufferSizeSamples();
        if (sr <= 0.0)
            sr = 48000.0;
        if (bs <= 0)
            bs = 512;
    }

    auto graph = std::make_unique<juce::AudioProcessorGraph>();
    graph->setPlayConfigDetails(2, 2, sr, bs);

    auto toneNode = graph->addNode(std::make_unique<engine::processors::TestToneProcessor>());
    auto outNode = graph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    if (auto* tone = dynamic_cast<engine::processors::TestToneProcessor*>(toneNode->getProcessor()))
    {
        tone->setAmplitude(0.25f);
        tone->setFrequency(440.0f);
        tone->setEnabled(true);
    }

    graph->addConnection({{toneNode->nodeID, 0}, {outNode->nodeID, 0}});
    graph->addConnection({{toneNode->nodeID, 1}, {outNode->nodeID, 1}});

    graph->prepareToPlay(sr, bs);
    return graph;
}

// Root component — hosts the hello-world text plus the three diagnostic
// buttons. Holds a reference to the AppContext so button handlers can
// drive the runtime, open the device-selector dialog, and open the
// plugin-list dialog.
class RootComponent : public juce::Component
{
public:
    explicit RootComponent(AppContext& ctxRef) : ctx_(ctxRef)
    {
        settingsButton_.setButtonText("Audio Settings...");
        settingsButton_.onClick = [this] { openAudioSettings(); };
        addAndMakeVisible(settingsButton_);

        toneButton_.setButtonText("Test Tone");
        toneButton_.setClickingTogglesState(true);
        toneButton_.onClick = [this] { onTestToneToggled(); };
        addAndMakeVisible(toneButton_);

        pluginListButton_.setButtonText("Plugin List...");
        pluginListButton_.onClick = [this] { openPluginList(); };
        addAndMakeVisible(pluginListButton_);

        setSize(720, 360);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));

        auto bounds = getLocalBounds().reduced(16);
        const int titleHeight = 60;
        const int bodyHeight = 40;
        auto titleArea = bounds.removeFromTop(titleHeight);
        auto bodyArea = bounds.removeFromTop(bodyHeight);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(32.0f, juce::Font::plain));
        g.drawFittedText("Pedalboard 3", titleArea, juce::Justification::centred, 1);

        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(14.0f, juce::Font::plain));
        g.drawFittedText("Phase 1.C.6 — Plugin List dialog wired up.", bodyArea, juce::Justification::centred, 1);
    }

    void resized() override
    {
        // Horizontal three-button strip near the bottom of the window.
        const int buttonHeight = 32;
        const int buttonGap = 12;
        const int numButtons = 3;

        auto area = getLocalBounds().reduced(16).removeFromBottom(buttonHeight + 16);
        area.removeFromBottom(16);
        const int totalWidth = juce::jmin(area.getWidth(), 660);
        const int eachWidth = (totalWidth - buttonGap * (numButtons - 1)) / numButtons;
        auto strip = area.withSizeKeepingCentre(totalWidth, buttonHeight);

        settingsButton_.setBounds(strip.removeFromLeft(eachWidth));
        strip.removeFromLeft(buttonGap);
        toneButton_.setBounds(strip.removeFromLeft(eachWidth));
        strip.removeFromLeft(buttonGap);
        pluginListButton_.setBounds(strip.removeFromLeft(eachWidth));
    }

private:
    void openAudioSettings()
    {
        // Standard JUCE device selector. 2-channel stereo in / 2-channel
        // stereo out keeps parity with AudioEngine::initialise.
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(*ctx_.audioEngine.getDeviceManager(),
                                                                             0,
                                                                             2, // min/max input channels
                                                                             0,
                                                                             2, // min/max output channels
                                                                             /*showMidiInputOptions*/ true,
                                                                             /*showMidiOutputSelector*/ false,
                                                                             /*showChannelsAsStereoPairs*/ true,
                                                                             /*hideAdvancedOptionsWithButton*/ false);
        selector->setSize(500, 420);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector.release());
        options.dialogTitle = "Audio Settings";
        options.dialogBackgroundColour = juce::Colour::fromRGB(0x2A, 0x2A, 0x2A);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void onTestToneToggled()
    {
        if (toneButton_.getToggleState())
        {
            auto graph = buildTestToneGraph(*ctx_.audioEngine.getDeviceManager());
            ctx_.audioEngine.loadGraph(std::move(graph));
        }
        else
        {
            ctx_.audioEngine.loadGraph(nullptr);
        }
    }

    void openPluginList()
    {
        // JUCE's standard plugin-list UI. Handles scanning (including
        // out-of-process where supported), per-format scan buttons, and
        // remove/enable per row. We don't pass a PropertiesFile for
        // persistence yet — that arrives in Phase 1.C.2 via Preferences.
        auto listComponent = std::make_unique<juce::PluginListComponent>(ctx_.pluginHost.getFormatManager(),
                                                                         ctx_.pluginHost.getKnownPluginList(),
                                                                         ctx_.pluginHost.getCrashedPluginsFile(),
                                                                         /*propertiesFile*/ nullptr,
                                                                         /*allowAsyncScanning*/ false);
        listComponent->setSize(720, 520);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(listComponent.release());
        options.dialogTitle = "Plugin List";
        options.dialogBackgroundColour = juce::Colour::fromRGB(0x2A, 0x2A, 0x2A);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    }

    AppContext& ctx_;
    juce::TextButton settingsButton_;
    juce::TextButton toneButton_;
    juce::TextButton pluginListButton_;
};

} // namespace

MainWindow::MainWindow(const juce::String& name, AppContext& context)
    : juce::DocumentWindow(
          name,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new RootComponent(context), true);
    setResizable(true, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace pedalboard3::ui
