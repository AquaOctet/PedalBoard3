#include "MainWindow.h"

#include "AppContext.h"
#include "engine/AudioEngine.h"
#include "engine/AudioGraphRuntime.h"
#include "engine/PluginHost.h"
#include "engine/processors/TestToneProcessor.h"
#include "session/PluginSlot.h"
#include "session/Preferences.h"
#include "session/Session.h"

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

// Root component — hosts the title plus the diagnostic + plugin-slot
// controls. Holds a reference to the AppContext so button handlers can
// drive the runtime, open the device-selector dialog, populate plugin
// lists, and load/unload plugins.
class RootComponent : public juce::Component, public juce::ChangeListener
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

        pluginCombo_.setTextWhenNothingSelected("(no plugins scanned — click Plugin List)");
        pluginCombo_.setTextWhenNoChoicesAvailable("(no plugins scanned — click Plugin List)");
        addAndMakeVisible(pluginCombo_);

        loadButton_.setButtonText("Load");
        loadButton_.onClick = [this] { onLoadClicked(); };
        addAndMakeVisible(loadButton_);

        clearButton_.setButtonText("Clear");
        clearButton_.onClick = [this] { onClearClicked(); };
        addAndMakeVisible(clearButton_);

        bypassButton_.setButtonText("Bypass");
        bypassButton_.setClickingTogglesState(true);
        bypassButton_.onClick = [this] { ctx_.pluginSlot.setBypassed(bypassButton_.getToggleState()); };
        addAndMakeVisible(bypassButton_);

        editButton_.setButtonText("Edit");
        editButton_.onClick = [this] { onEditClicked(); };
        addAndMakeVisible(editButton_);

        statusLabel_.setJustificationType(juce::Justification::centredLeft);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(statusLabel_);

        ctx_.pluginHost.getKnownPluginList().addChangeListener(this);
        ctx_.pluginSlot.addChangeListener(this);
        ctx_.session.addChangeListener(this);

        rebuildPluginCombo();
        // Sync once with the current Session/PluginSlot state so a fast
        // restore-on-launch that completed before MainWindow was built
        // still updates the UI on the next paint.
        refreshSlotControls();

        setSize(720, 420);
    }

    ~RootComponent() override
    {
        ctx_.pluginHost.getKnownPluginList().removeChangeListener(this);
        ctx_.pluginSlot.removeChangeListener(this);
        ctx_.session.removeChangeListener(this);
        editorWindow_.reset();
    }

    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if (source == &ctx_.pluginHost.getKnownPluginList())
            rebuildPluginCombo();
        else if (source == &ctx_.pluginSlot || source == &ctx_.session)
            refreshSlotControls();
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
        const auto subtitle = juce::String("Phase 1.D.2 — queued patch-switch with all-notes-off swap.");
        g.drawFittedText(subtitle, bodyArea, juce::Justification::centred, 1);
    }

    void resized() override
    {
        const int rowHeight = 32;
        const int gap = 12;

        auto area = getLocalBounds().reduced(16);
        // Top region (paint draws title/body; reserve their space).
        area.removeFromTop(60 + 40 + 8);

        // Plugin slot row: ComboBox | Load | Clear | Bypass | Edit
        auto slotRow = area.removeFromTop(rowHeight);
        const int btnW = 80;
        editButton_.setBounds(slotRow.removeFromRight(btnW));
        slotRow.removeFromRight(gap);
        bypassButton_.setBounds(slotRow.removeFromRight(btnW));
        slotRow.removeFromRight(gap);
        clearButton_.setBounds(slotRow.removeFromRight(btnW));
        slotRow.removeFromRight(gap);
        loadButton_.setBounds(slotRow.removeFromRight(btnW));
        slotRow.removeFromRight(gap);
        pluginCombo_.setBounds(slotRow);

        area.removeFromTop(gap);

        // Status row.
        auto statusRow = area.removeFromTop(28);
        statusLabel_.setBounds(statusRow);

        // Bottom strip with the original three diagnostic buttons.
        auto bottom = getLocalBounds().reduced(16).removeFromBottom(rowHeight + 16);
        bottom.removeFromBottom(16);
        const int totalWidth = juce::jmin(bottom.getWidth(), 660);
        const int eachWidth = (totalWidth - gap * 2) / 3;
        auto strip = bottom.withSizeKeepingCentre(totalWidth, rowHeight);

        settingsButton_.setBounds(strip.removeFromLeft(eachWidth));
        strip.removeFromLeft(gap);
        toneButton_.setBounds(strip.removeFromLeft(eachWidth));
        strip.removeFromLeft(gap);
        pluginListButton_.setBounds(strip.removeFromLeft(eachWidth));
    }

private:
    void rebuildPluginCombo()
    {
        pluginCombo_.clear(juce::dontSendNotification);
        comboItemDescriptions_.clearQuick();

        const auto plugins = ctx_.pluginHost.getKnownPlugins();
        int id = 1;
        for (const auto& desc : plugins)
        {
            pluginCombo_.addItem(desc.name + "  [" + desc.pluginFormatName + "]", id++);
            comboItemDescriptions_.add(desc);
        }
    }

    void refreshSlotControls()
    {
        const bool loaded = ctx_.pluginSlot.isLoaded();
        const auto state = ctx_.session.getState();
        const bool idle = (state == session::Session::State::Idle);
        auto* hostedPlugin = ctx_.pluginSlot.getHostedPlugin();

        // Load button is disabled while the worker is building or the
        // message thread is mid-swap — re-arm coalescing applies, but
        // the user shouldn't be encouraged to spam clicks.
        loadButton_.setEnabled(idle);
        clearButton_.setEnabled(loaded && idle);
        bypassButton_.setEnabled(loaded);
        editButton_.setEnabled(loaded && hostedPlugin != nullptr && hostedPlugin->hasEditor());
        bypassButton_.setToggleState(ctx_.pluginSlot.isBypassed(), juce::dontSendNotification);

        switch (state)
        {
            case session::Session::State::Building:
                statusLabel_.setText("Loading...", juce::dontSendNotification);
                break;
            case session::Session::State::Swapping:
                statusLabel_.setText("Swapping...", juce::dontSendNotification);
                break;
            case session::Session::State::Idle:
            default:
            {
                const auto err = ctx_.session.getLastError();
                if (err.isNotEmpty() && !loaded)
                    statusLabel_.setText("Load failed: " + err, juce::dontSendNotification);
                else if (loaded)
                    statusLabel_.setText("Loaded: " + ctx_.pluginSlot.getLoadedName(), juce::dontSendNotification);
                else
                    statusLabel_.setText("No plugin loaded", juce::dontSendNotification);
                break;
            }
        }

        // Close any editor window for a no-longer-loaded plugin.
        if (!loaded)
            editorWindow_.reset();
    }

    void onLoadClicked()
    {
        const int selected = pluginCombo_.getSelectedId(); // 1-based
        if (selected < 1 || selected > comboItemDescriptions_.size())
        {
            const auto opts = juce::MessageBoxOptions{}
                                  .withIconType(juce::MessageBoxIconType::WarningIcon)
                                  .withTitle("Load plugin")
                                  .withMessage("Pick a plugin from the list first.")
                                  .withButton("OK");
            juce::AlertWindow::showAsync(opts, nullptr);
            return;
        }

        const auto& desc = comboItemDescriptions_.getReference(selected - 1);

        // Close any open editor for the previous plugin before swap.
        editorWindow_.reset();

        // Async dispatch through the patch-switch state machine. Errors
        // surface via the change-listener path (refreshSlotControls reads
        // session::Session::getLastError on the next Idle transition).
        ctx_.session.queueLoad(desc);
    }

    void onClearClicked()
    {
        editorWindow_.reset();
        ctx_.session.queueUnload();
    }

    void onEditClicked()
    {
        auto* plugin = ctx_.pluginSlot.getHostedPlugin();
        if (plugin == nullptr || !plugin->hasEditor())
            return;

        // If an editor window is already open, just bring it to front.
        if (editorWindow_ != nullptr)
        {
            editorWindow_->toFront(true);
            return;
        }

        auto* editor = plugin->createEditorIfNeeded();
        if (editor == nullptr)
            return;

        // Drop the editor when its window closes — JUCE expects the host
        // to delete the editor before the plugin instance dies.
        auto onClose = [this] { editorWindow_.reset(); };
        editorWindow_ = std::make_unique<EditorWindow>(ctx_.pluginSlot.getLoadedName(), editor, std::move(onClose));
    }

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
            // Test tone replaces any plugin in the slot — drop slot state
            // first so the UI shows "no plugin loaded".
            ctx_.pluginSlot.unload();
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
        // remove/enable per row. The PropertiesFile pointer lets JUCE
        // auto-save user edits (enable/disable, blacklist) to our
        // session::Preferences store.
        auto listComponent = std::make_unique<juce::PluginListComponent>(ctx_.pluginHost.getFormatManager(),
                                                                         ctx_.pluginHost.getKnownPluginList(),
                                                                         ctx_.pluginHost.getCrashedPluginsFile(),
                                                                         ctx_.preferences.getStore(),
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

    // Holds the plugin's AudioProcessorEditor in a DocumentWindow and
    // calls a callback on close so the host can drop its handle.
    class EditorWindow : public juce::DocumentWindow
    {
    public:
        EditorWindow(const juce::String& title, juce::AudioProcessorEditor* editor, std::function<void()> onClose)
            : juce::DocumentWindow(title,
                                   juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                       juce::ResizableWindow::backgroundColourId),
                                   juce::DocumentWindow::closeButton),
              onClose_(std::move(onClose))
        {
            setUsingNativeTitleBar(true);
            setResizable(editor->isResizable(), false);
            setContentOwned(editor, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            if (onClose_)
                onClose_();
        }

    private:
        std::function<void()> onClose_;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
    };

    AppContext& ctx_;

    juce::ComboBox pluginCombo_;
    juce::Array<juce::PluginDescription> comboItemDescriptions_;
    juce::TextButton loadButton_;
    juce::TextButton clearButton_;
    juce::TextButton bypassButton_;
    juce::TextButton editButton_;
    juce::Label statusLabel_;

    std::unique_ptr<EditorWindow> editorWindow_;

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
