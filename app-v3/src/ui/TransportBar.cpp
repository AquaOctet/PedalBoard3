#include "TransportBar.h"

#include "ColourScheme.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/processors/TestToneProcessor.h"
#include "session/Preferences.h"
#include "session/Session.h"
#include "SignalCanvas.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

namespace pedalboard3::ui
{

namespace
{
constexpr int kBarPadding = 12;
constexpr int kButtonGap = 6;
constexpr int kAddButtonWidth = 130;
constexpr int kSettingsButtonWidth = 140;
constexpr int kToneButtonWidth = 110;
constexpr int kPluginListButtonWidth = 130;

// Build a minimal graph that routes a TestToneProcessor straight to
// the master output. Lifted from the previous MainWindow.cpp (the
// transitional UI's onTestToneToggled handler). Prepared at the
// current device's sample rate / buffer or 48k/512 fallback.
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
} // namespace

TransportBar::TransportBar(AppContext& ctx, SignalCanvas& canvas) : ctx_(ctx), canvas_(canvas)
{
    setOpaque(true);

    addPluginButton_.onClick = [this] { openAddPluginMenu(); };
    settingsButton_.onClick = [this] { openAudioSettings(); };
    pluginListButton_.onClick = [this] { openPluginList(); };

    toneButton_.setClickingTogglesState(true);
    toneButton_.onClick = [this] { onTestToneToggled(); };

    addAndMakeVisible(addPluginButton_);
    addAndMakeVisible(settingsButton_);
    addAndMakeVisible(toneButton_);
    addAndMakeVisible(pluginListButton_);

    titleLabel_.setText("Pedalboard 3", juce::dontSendNotification);
    titleLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    titleLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(titleLabel_);

    deviceInfoLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    deviceInfoLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    deviceInfoLabel_.setJustificationType(juce::Justification::centred);
    deviceInfoLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(deviceInfoLabel_);

    refreshDeviceInfo();
    updateAddPluginEnabled();

    ctx_.session.addChangeListener(this);
}

TransportBar::~TransportBar()
{
    ctx_.session.removeChangeListener(this);
}

void TransportBar::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Session state may have transitioned; Add Plugin is enabled only
    // when Idle and Test Tone is off (otherwise the chain isn't the
    // live graph being driven by the engine).
    updateAddPluginEnabled();
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void TransportBar::resized()
{
    auto bounds = getLocalBounds().reduced(kBarPadding, 6);

    // Right cluster: Plugin List | Test Tone | Audio Settings.
    auto right =
        bounds.removeFromRight(kPluginListButtonWidth + kToneButtonWidth + kSettingsButtonWidth + 2 * kButtonGap);
    settingsButton_.setBounds(right.removeFromLeft(kSettingsButtonWidth));
    right.removeFromLeft(kButtonGap);
    toneButton_.setBounds(right.removeFromLeft(kToneButtonWidth));
    right.removeFromLeft(kButtonGap);
    pluginListButton_.setBounds(right.removeFromLeft(kPluginListButtonWidth));

    bounds.removeFromRight(kBarPadding);

    // Left cluster: title text + Add Plugin button.
    auto left = bounds.removeFromLeft(120 + kAddButtonWidth + kButtonGap);
    titleLabel_.setBounds(left.removeFromLeft(120));
    left.removeFromLeft(kButtonGap);
    addPluginButton_.setBounds(left.removeFromLeft(kAddButtonWidth));

    // Centre: device info fills the rest.
    deviceInfoLabel_.setBounds(bounds);
}

void TransportBar::refreshDeviceInfo()
{
    auto* dm = ctx_.audioEngine.getDeviceManager();
    if (dm == nullptr)
    {
        deviceInfoLabel_.setText("(no audio device)", juce::dontSendNotification);
        return;
    }

    auto* dev = dm->getCurrentAudioDevice();
    if (dev == nullptr)
    {
        deviceInfoLabel_.setText("(no audio device — open Audio Settings)", juce::dontSendNotification);
        return;
    }

    juce::String text = dev->getName() + "  •  ";
    text += juce::String(static_cast<int>(dev->getCurrentSampleRate())) + " Hz";
    text += "  /  " + juce::String(dev->getCurrentBufferSizeSamples()) + " samples";
    text += "  •  CPU —  •  Latency —";
    deviceInfoLabel_.setText(text, juce::dontSendNotification);
}

void TransportBar::updateAddPluginEnabled()
{
    // Disable while Session is mid-build/swap, AND while Test Tone is
    // on — the tone graph is not Session-managed, so adding a plugin
    // would either be a no-op (post-fix) or stale-pointer-crash (pre-fix).
    const bool idle = ctx_.session.getState() == session::Session::State::Idle;
    const bool toneOn = toneButton_.getToggleState();
    addPluginButton_.setEnabled(idle && !toneOn);
}

void TransportBar::openAddPluginMenu()
{
    // Effects-only filter: v3.0 ships as a live-performance effect rack;
    // instruments are sidestepped per the master-plan scope and to dodge
    // the heavyweight license/auth crashes that hit Current.vst3 and
    // Kontakt-class plugins during construction. See
    // PluginHost::getKnownEffects for the rationale.
    const auto effects = ctx_.pluginHost.getKnownEffects();
    if (effects.isEmpty())
    {
        const auto allKnown = ctx_.pluginHost.getKnownPlugins().size();
        const auto message = allKnown > 0
                                 ? "All scanned plugins are instruments — Pedalboard 3 only loads effects in v3.0. "
                                   "Open the Plugin List dialog to scan for effect plugins."
                                 : "No plugins scanned yet. Open the Plugin List dialog to scan.";
        const auto opts = juce::MessageBoxOptions{}
                              .withIconType(juce::MessageBoxIconType::InfoIcon)
                              .withTitle("Add plugin")
                              .withMessage(message)
                              .withButton("OK");
        juce::AlertWindow::showAsync(opts, nullptr);
        return;
    }

    juce::PopupMenu menu;
    int id = 1;
    for (const auto& desc : effects)
        menu.addItem(id++, desc.name + "  [" + desc.pluginFormatName + "]");

    auto opts = juce::PopupMenu::Options{}.withTargetComponent(addPluginButton_);
    menu.showMenuAsync(opts,
                       [this, effects](int chosen)
                       {
                           if (chosen <= 0 || chosen > effects.size())
                               return;
                           ctx_.session.addPlugin(effects.getReference(chosen - 1));
                       });
}

void TransportBar::openAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(*ctx_.audioEngine.getDeviceManager(),
                                                                         0,
                                                                         2,
                                                                         0,
                                                                         2,
                                                                         /*showMidiInputOptions*/ true,
                                                                         /*showMidiOutputSelector*/ false,
                                                                         /*showChannelsAsStereoPairs*/ true,
                                                                         /*hideAdvancedOptionsWithButton*/ false);
    selector->setSize(500, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = tokens::bgPopover;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void TransportBar::openPluginList()
{
    // Pre-emptive crash-loop protection. JUCE's PluginListComponent ctor
    // reads the dead-man's-pedal file via applyBlacklistingsFromDeadMansPedal,
    // adds entries to KnownPluginList's blacklist via an async change
    // message, THEN deletes the file. The async change message is then
    // routed through PreferencesPersister to persist the new blacklist.
    //
    // That chain is fragile: if a second crash hits before the listener
    // flushes preferences (e.g. user clicks Scan immediately), the
    // dead-man's-pedal file is already gone AND the blacklist isn't on
    // disk. Next launch has no record of the bad plugin and re-tries it.
    //
    // Pre-empt the chain: read the dead-man's-pedal file ourselves,
    // populate the blacklist, flush preferences SYNCHRONOUSLY, then show
    // the dialog. JUCE's later applyBlacklistings is a no-op (addToBlacklist
    // checks for duplicates and short-circuits), but the file gets deleted
    // either way so we close the loop the same.
    auto& list = ctx_.pluginHost.getKnownPluginList();
    bool blacklistChanged = false;

    const auto crashedFile = ctx_.pluginHost.getCrashedPluginsFile();
    if (crashedFile.existsAsFile())
    {
        const auto contents = crashedFile.loadFileAsString();
        for (const auto& line : juce::StringArray::fromLines(contents))
        {
            const auto trimmed = line.trim();
            if (trimmed.isEmpty())
                continue;
            if (!list.getBlacklistedFiles().contains(trimmed))
            {
                list.addToBlacklist(trimmed);
                blacklistChanged = true;
            }
        }
    }

    // Effects-only host (master plan §4.2.D — see PluginHost::getKnownEffects
    // for the rationale): instruments in v3.0 are exactly the plugins most
    // likely to crash during scan / destruction (Kontakt's analytics worker
    // throws from BusinessAnalytics::deInit on a background thread after
    // scanAndAddFile already returned, so the dead-man's-pedal file no
    // longer points at Kontakt by the time the abort fires). Auto-blacklist
    // every isInstrument plugin we know about so re-scans skip them. The
    // user can still see them in the Plugin List dialog as blacklisted
    // entries, and can manually un-blacklist if v3.1 adds instrument
    // hosting.
    for (const auto& desc : list.getTypes())
    {
        if (desc.isInstrument && !list.getBlacklistedFiles().contains(desc.fileOrIdentifier))
        {
            list.addToBlacklist(desc.fileOrIdentifier);
            blacklistChanged = true;
        }
    }

    if (blacklistChanged)
    {
        // addToBlacklist sends an async change message, so the
        // PreferencesPersister listener won't have fired yet. Mirror
        // its work synchronously here so flush() actually writes
        // the new blacklist (rather than the pre-update state).
        if (auto xml = ctx_.pluginHost.createKnownListXml())
            ctx_.preferences.setKnownPluginList(*xml);
        ctx_.preferences.flush();
    }

    auto listComponent = std::make_unique<juce::PluginListComponent>(ctx_.pluginHost.getFormatManager(),
                                                                     ctx_.pluginHost.getKnownPluginList(),
                                                                     ctx_.pluginHost.getCrashedPluginsFile(),
                                                                     ctx_.preferences.getStore(),
                                                                     /*allowAsyncScanning*/ false);
    listComponent->setSize(720, 520);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(listComponent.release());
    options.dialogTitle = "Plugin List";
    options.dialogBackgroundColour = tokens::bgPopover;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void TransportBar::onTestToneToggled()
{
    if (toneButton_.getToggleState())
    {
        // CALLER-MANAGED EDITOR LIFECYCLE (D1b / AC-UI-P): close
        // every open EditorWindow synchronously BEFORE Session
        // destroys the underlying plugin instances on clearPatch.
        canvas_.closeAllEditors();
        ctx_.session.clearPatch();
        auto graph = buildTestToneGraph(*ctx_.audioEngine.getDeviceManager());
        ctx_.audioEngine.loadGraph(std::move(graph));
    }
    else
    {
        // Toggle OFF: route through Session::clearPatch instead of
        // audioEngine.loadGraph(nullptr). The latter would leave
        // Session::liveGraph_ pointing at the now-destroyed tone graph
        // (a dangling pointer); the next addPlugin would crash with a
        // null vtable read inside juce::AudioProcessorGraph::Pimpl.
        // clearPatch goes through the worker → commitLoadPatch path,
        // which builds a fresh empty-passthrough graph and updates
        // liveGraph_ atomically. The brief audible silence between the
        // tone stopping and the empty graph swapping in is acceptable.
        ctx_.session.clearPatch();
    }
    updateAddPluginEnabled();
}

} // namespace pedalboard3::ui
