#include "TransportBar.h"

#include "ColourScheme.h"
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/PluginScanner.h"
#include "engine/processors/TestToneProcessor.h"
#include "session/PdlSerializer.h"
#include "session/Preferences.h"
#include "session/Session.h"
#include "session/v2_import/PdlV2Importer.h"
#include "SignalCanvas.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace pedalboard3::ui
{

namespace
{
constexpr int kBarPadding = 12;
constexpr int kButtonGap = 6;
constexpr int kFileButtonWidth = 64;
constexpr int kAddButtonWidth = 130;
constexpr int kOscLabelWidth = 34;
constexpr int kOscEditorWidth = 50;
constexpr int kSettingsButtonWidth = 140;
constexpr int kToneButtonWidth = 100;
constexpr int kRescanButtonWidth = 130;
constexpr int kPluginListButtonWidth = 110;

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

// Resolve an imported (e.g. Pedalboard2/VST2) plugin descriptor to an installed
// plugin by display name — case-insensitive, effects preferred over instruments.
// Returns nullptr when nothing matches. Lets a v2 import reconnect to the user's
// installed VST3/AU build of the same plugin even though the format differs.
const juce::PluginDescription* findKnownByName(const juce::Array<juce::PluginDescription>& known,
                                               const juce::String& name)
{
    const juce::PluginDescription* best = nullptr;
    for (const auto& d : known)
    {
        if (!d.name.equalsIgnoreCase(name))
            continue;
        if (!d.isInstrument)
            return &d; // an effect match is exactly what we want
        if (best == nullptr)
            best = &d; // remember an instrument match as a last resort
    }
    return best;
}

struct LoadedPatch
{
    std::unique_ptr<session::Patch> patch;
    juce::StringArray warnings;
    juce::String error;
};

// Load a patch file for File > Open, transparently handling both v3 .pdl and
// legacy Pedalboard2 (.pdl v2) files. For a v2 file it imports the topology and
// resolves each plugin to an installed one by name (dropping unmatched, with a
// warning); state is not migrated across the VST2 -> VST3 format change.
LoadedPatch loadOpenedPatch(const juce::File& file, engine::PluginHost& host)
{
    LoadedPatch out;

    auto xml = juce::parseXML(file);
    if (xml != nullptr && session::v2::isPedalboard2Patch(*xml))
    {
        auto imp = session::v2::importPedalboard2Patch(*xml);
        out.warnings = imp.warnings;
        out.error = imp.error;
        if (imp.patch == nullptr)
            return out;

        const auto known = host.getKnownPlugins();
        std::vector<session::PatchNode> kept;
        for (auto& node : imp.patch->nodes)
        {
            if (const auto* match = findKnownByName(known, node.desc.name))
            {
                node.desc = *match;    // resolve to the installed plugin
                node.state.setSize(0); // cross-format; state already not migrated
                kept.push_back(std::move(node));
            }
            else
            {
                out.warnings.add("Skipped \"" + node.desc.name +
                                 "\" - not found in your scanned plugins (scan it, or no installed equivalent).");
            }
        }
        imp.patch->nodes = std::move(kept);
        imp.patch->connections = session::linearChainConnections(imp.patch->nodes);
        out.patch = std::move(imp.patch);
        return out;
    }

    auto loaded = session::pdl::readPatch(file);
    out.patch = std::move(loaded.patch);
    out.error = loaded.error;
    return out;
}
} // namespace

TransportBar::TransportBar(AppContext& ctx, SignalCanvas& canvas) : ctx_(ctx), canvas_(canvas)
{
    setOpaque(true);

    fileButton_.onClick = [this] { openFileMenu(); };
    addPluginButton_.onClick = [this] { openAddPluginMenu(); };
    settingsButton_.onClick = [this] { openAudioSettings(); };
    rescanButton_.onClick = [this] { rescanPlugins(); };
    pluginListButton_.onClick = [this] { openPluginList(); };

    toneButton_.setClickingTogglesState(true);
    toneButton_.onClick = [this] { onTestToneToggled(); };

    oscPortLabel_.setText("OSC:", juce::dontSendNotification);
    oscPortLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    oscPortLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    oscPortLabel_.setJustificationType(juce::Justification::centredRight);
    oscPortLabel_.setInterceptsMouseClicks(false, false);

    oscPortEditor_.setInputRestrictions(5, "0123456789");
    oscPortEditor_.setJustification(juce::Justification::centred);
    oscPortEditor_.setText(juce::String(ctx_.preferences.getOscPort()), juce::dontSendNotification);
    oscPortEditor_.setTooltip("OSC listener port (1-65535). Press Enter to apply.");
    oscPortEditor_.onReturnKey = [this] { applyOscPortChange(); };
    oscPortEditor_.onFocusLost = [this] { applyOscPortChange(); };

    addAndMakeVisible(fileButton_);
    addAndMakeVisible(addPluginButton_);
    addAndMakeVisible(oscPortLabel_);
    addAndMakeVisible(oscPortEditor_);
    addAndMakeVisible(settingsButton_);
    addAndMakeVisible(toneButton_);
    addAndMakeVisible(rescanButton_);
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

    // Join any in-flight rescan before AppContext/Session/PluginHost tear down
    // (crash-hunt #8). The scan worker holds references into ctx_; letting it
    // outlive us was the fire-and-forget use-after-free. signalThreadShouldExit
    // trips the scan's abort predicate, which breaks the plugin loop and kills
    // any in-flight subprocess at the next ~100ms poll slice, so stopThread(-1)
    // joins promptly (bounded by one poll slice, not the per-plugin timeout)
    // without force-killing the worker.
    if (rescanThread_ != nullptr)
    {
        rescanThread_->signalThreadShouldExit();
        rescanThread_->stopThread(-1);
        rescanThread_.reset();
    }
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

    // Right cluster: OSC port | Audio Settings | Test Tone | Rescan | Plugin List.
    auto right = bounds.removeFromRight(kOscLabelWidth + kOscEditorWidth + kSettingsButtonWidth + kToneButtonWidth +
                                        kRescanButtonWidth + kPluginListButtonWidth + 4 * kButtonGap);
    oscPortLabel_.setBounds(right.removeFromLeft(kOscLabelWidth));
    oscPortEditor_.setBounds(right.removeFromLeft(kOscEditorWidth));
    right.removeFromLeft(kButtonGap);
    settingsButton_.setBounds(right.removeFromLeft(kSettingsButtonWidth));
    right.removeFromLeft(kButtonGap);
    toneButton_.setBounds(right.removeFromLeft(kToneButtonWidth));
    right.removeFromLeft(kButtonGap);
    rescanButton_.setBounds(right.removeFromLeft(kRescanButtonWidth));
    right.removeFromLeft(kButtonGap);
    pluginListButton_.setBounds(right.removeFromLeft(kPluginListButtonWidth));

    bounds.removeFromRight(kBarPadding);

    // Left cluster: title text + File + Add Plugin buttons.
    auto left = bounds.removeFromLeft(120 + kFileButtonWidth + kAddButtonWidth + 2 * kButtonGap);
    titleLabel_.setBounds(left.removeFromLeft(120));
    left.removeFromLeft(kButtonGap);
    fileButton_.setBounds(left.removeFromLeft(kFileButtonWidth));
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
        // Surface the actual reason audio is silent (e.g. a driver that
        // failed to open) rather than a generic placeholder — otherwise
        // the failure is invisible except for a log line.
        const auto& err = ctx_.audioEngine.getLastInitError();
        deviceInfoLabel_.setText(err.isNotEmpty() ? ("Audio device error: " + err + " — open Audio Settings")
                                                  : "(no audio device — open Audio Settings)",
                                 juce::dontSendNotification);
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
    const auto internals = ctx_.pluginHost.getInternalNodeTypes();

    juce::PopupMenu menu;

    // Effects (scanned plugins) take ids 1..effects.size(). A disabled hint
    // stands in when none are scanned — built-in nodes are still available below,
    // so an empty plugin list must NOT block the Add menu.
    int id = 1;
    if (effects.isEmpty())
    {
        const auto allKnown = ctx_.pluginHost.getKnownPlugins().size();
        menu.addItem(juce::PopupMenu::Item(allKnown > 0 ? "(All scanned plugins are instruments)"
                                                        : "(No effects scanned — open Plugin List)")
                         .setEnabled(false));
    }
    else
    {
        for (const auto& desc : effects)
            menu.addItem(id++, desc.name + "  [" + desc.pluginFormatName + "]");
    }

    // Built-in nodes (Level, VU, ...) in their own submenu, with ids offset past
    // the effects so the two id spaces never collide.
    const int internalIdBase = id;
    juce::PopupMenu internalMenu;
    for (int i = 0; i < internals.size(); ++i)
        internalMenu.addItem(internalIdBase + i, internals.getReference(i).displayName);
    menu.addSubMenu("Built-in", internalMenu);

    auto opts = juce::PopupMenu::Options{}.withTargetComponent(addPluginButton_);
    menu.showMenuAsync(opts,
                       [this, effects, internals, internalIdBase](int chosen)
                       {
                           if (chosen <= 0)
                               return;
                           if (chosen < internalIdBase)
                           {
                               if (chosen <= effects.size())
                                   ctx_.session.addPlugin(effects.getReference(chosen - 1));
                               return;
                           }
                           const int internalIdx = chosen - internalIdBase;
                           if (internalIdx >= 0 && internalIdx < internals.size())
                               ctx_.session.addInternalNode(internals.getReference(internalIdx).typeId);
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

void TransportBar::applyOscPortChange()
{
    const int port = oscPortEditor_.getText().getIntValue();
    if (port <= 0 || port > 65535)
    {
        // Reject a nonsense port back to the last-good persisted value rather
        // than attempting a bind with it.
        oscPortEditor_.setText(juce::String(ctx_.preferences.getOscPort()), juce::dontSendNotification);
        return;
    }

    // rebind() drops the previous (working) binding before attempting the new
    // one, so a failure leaves OSC dead either way -- but persist only on
    // success, so a rejected port never overwrites the last-good saved value.
    const auto error = ctx_.oscServer.rebind(port, ctx_.preferences.getOscMulticastAddress());
    if (error.isEmpty())
    {
        ctx_.preferences.setOscPort(port);
        ctx_.preferences.flush();
    }

    // Surface a bind failure (e.g. port already in use) inline via the
    // field's colour/tooltip rather than a blocking dialog — the control
    // layer keeps working regardless of OSC availability (App::initialise
    // treats a bind failure the same way).
    oscPortEditor_.setColour(juce::TextEditor::textColourId,
                             error.isNotEmpty() ? tokens::accentDestructive : tokens::fgPrimary);
    oscPortEditor_.setTooltip(error.isNotEmpty() ? error
                                                 : juce::String("OSC listener port (1-65535). Press Enter to apply."));
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

    // Instrument removal + blacklist now happens centrally in PluginHost's
    // ChangeListener (see PluginHost::changeListenerCallback). It fires on
    // every KnownPluginList mutation regardless of source — our subprocess
    // scanner, juce::PluginDirectoryScanner used internally by JUCE's
    // PluginListComponent, scanDirectory, or loadKnownList. By the time
    // this dialog opens, isInstrument descriptors have already been
    // stripped and their files blacklisted, so the dialog renders a
    // clean list and the dialog's own Scan button benefits from the
    // listener too (any new instrument JUCE adds during in-dialog
    // scanning gets removed and blacklisted on the very next message
    // loop tick).

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

namespace
{

// Self-deleting ThreadWithProgressWindow that owns its own PluginScanner
// and posts the post-scan summary on the message thread. We can't use
// ThreadWithProgressWindow::runThread() — that's gated by
// JUCE_MODAL_LOOPS_PERMITTED, which Pedalboard 3 disables (modal
// loops on the message thread are forbidden in a real-time audio app).
// launchThread() is non-blocking; threadComplete() fires on the
// message thread when the worker exits, and that's where we persist
// preferences, surface results to the user, and self-delete.
class PluginRescanThread : public juce::ThreadWithProgressWindow
{
public:
    // `blacklistSnapshot` is captured by the caller on the message thread and
    // handed in by value: the worker must never read the live KnownPluginList
    // (crash-hunt #6). `onDone` fires on the message thread when the worker
    // exits — the owner (TransportBar) uses it to release its unique_ptr.
    PluginRescanThread(AppContext& ctx,
                       juce::File helperBinary,
                       juce::StringArray blacklistSnapshot,
                       std::function<void()> onDone)
        : juce::ThreadWithProgressWindow("Scanning plugins...", /*hasProgressBar*/ true, /*hasCancelButton*/ true),
          ctx_(ctx), scanner_(ctx_.pluginHost, std::move(helperBinary)),
          blacklistSnapshot_(std::move(blacklistSnapshot)), onDone_(std::move(onDone))
    {
        setStatusMessage("Enumerating plugin search paths...");
    }

    void run() override
    {
        // Worker thread: subprocess scans ONLY. Collect results here and do
        // NOT touch KnownPluginList — it is committed on the message thread in
        // threadComplete(). The blacklist to skip was snapshotted on the
        // message thread and passed in, so the worker never reads the live
        // list (its ChangeListeners run on the message thread; crash-hunt #6).
        results_ = scanner_.scanAllDefaultPaths(
            [this](int idx, int total, const juce::String& path)
            {
                if (threadShouldExit() || total == 0)
                    return;
                setProgress(static_cast<double>(idx) / static_cast<double>(total));
                setStatusMessage("Scanning: " + juce::File(path).getFileName());
            },
            blacklistSnapshot_,
            // Abort predicate: lets ~TransportBar's signalThreadShouldExit()
            // cut the scan short (between plugins and mid-subprocess) so the
            // join is bounded to ~one poll slice, not up to 30s (crash-hunt #8).
            [this] { return threadShouldExit(); });
    }

    void threadComplete(bool userPressedCancel) override
    {
        // Message thread. Commit the collected results to the KnownPluginList
        // HERE — this is the single writer of the list on the scan path, so it
        // runs serialised with the list's ChangeListeners. Then persist +
        // summarise. Note: no `delete this` — TransportBar owns this object
        // and releases it via onDone_ (crash-hunt #8: a quit mid-scan must be
        // able to join us, which a self-deleting thread can't support).
        added_ = scanner_.applyScanResults(results_);

        if (auto xml = ctx_.pluginHost.createKnownListXml())
            ctx_.preferences.setKnownPluginList(*xml);
        ctx_.preferences.flush();

        const auto& blacklist = ctx_.pluginHost.getKnownPluginList().getBlacklistedFiles();
        juce::String summary;
        if (userPressedCancel)
            summary = "Scan cancelled. Partial results saved.\n\n";
        summary += "Plugins found: " + juce::String(added_) + "\n";
        summary += "Plugins skipped (blacklist): " + juce::String(blacklist.size());

        const auto opts = juce::MessageBoxOptions{}
                              .withIconType(juce::MessageBoxIconType::InfoIcon)
                              .withTitle(userPressedCancel ? "Scan cancelled" : "Scan complete")
                              .withMessage(summary)
                              .withButton("OK");
        juce::AlertWindow::showAsync(opts, nullptr);

        if (onDone_)
            onDone_();
    }

private:
    AppContext& ctx_;
    engine::PluginScanner scanner_;
    juce::StringArray blacklistSnapshot_;                    // message-thread capture
    std::vector<engine::PluginScanner::ScanResult> results_; // worker -> message thread
    std::function<void()> onDone_;                           // fires on message thread at exit
    int added_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginRescanThread)
};

} // namespace

void TransportBar::rescanPlugins()
{
    // One scan at a time — the owned thread is single-slot.
    if (rescanThread_ != nullptr)
        return;

    auto helper = engine::PluginScanner::findHelperBinary();
    if (!helper.existsAsFile())
    {
        // Helper missing — happens on dev builds where POST_BUILD copy hasn't
        // run, or installs missing the bundled helper. Do NOT route the user
        // to the in-process scan (crash-hunt #6): JUCE's PluginListComponent
        // scanner constructs every plugin in the host process, so a
        // construction-crashing plugin not on the floor list takes the host
        // down. Scanning is simply unavailable until the helper is present.
        const auto opts = juce::MessageBoxOptions{}
                              .withIconType(juce::MessageBoxIconType::WarningIcon)
                              .withTitle("Plugin scanning unavailable")
                              .withMessage("pb3-scanner-helper binary was not found next to the host executable, "
                                           "so out-of-process scanning is disabled. Reinstall Pedalboard 3 (or "
                                           "rebuild so the helper is copied next to the app) to scan for plugins. "
                                           "Already-known plugins remain usable.")
                              .withButton("OK");
        juce::AlertWindow::showAsync(opts, nullptr);
        return;
    }

    // Capture the blacklist HERE, on the message thread, and hand it to the
    // worker by value (crash-hunt #6): the worker must not read the live
    // KnownPluginList, whose ChangeListeners mutate it on the message thread.
    auto blacklistSnapshot = ctx_.pluginHost.getKnownPluginList().getBlacklistedFiles();

    // Owned, not fire-and-forget: ~TransportBar joins it on quit. threadComplete
    // fires onDone on the message thread; defer the unique_ptr reset (via a
    // SafePointer, in case the bar was destroyed in between) so we don't
    // destroy the thread from inside its own completion callback.
    juce::Component::SafePointer<TransportBar> safeThis(this);
    rescanThread_ = std::make_unique<PluginRescanThread>(ctx_,
                                                         std::move(helper),
                                                         std::move(blacklistSnapshot),
                                                         [safeThis]
                                                         {
                                                             juce::MessageManager::callAsync(
                                                                 [safeThis]
                                                                 {
                                                                     if (safeThis != nullptr)
                                                                         safeThis->rescanThread_.reset();
                                                                 });
                                                         });
    rescanThread_->launchThread();
}

void TransportBar::openFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Open Patch...");
    menu.addItem(2, "Save Patch As...");
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(fileButton_),
                       [this](int choice)
                       {
                           if (choice == 1)
                               openPatch();
                           else if (choice == 2)
                               savePatch();
                       });
}

void TransportBar::savePatch()
{
    // Snapshot the chain WITH live plugin state so a later load restores
    // knob settings (master plan §4.5 / §4.2.B).
    auto snapshot = ctx_.session.getActivePatchWithState();

    const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Pedalboard 3");
    fileChooser_ =
        std::make_unique<juce::FileChooser>("Save Pedalboard Patch", dir.getChildFile("Untitled.pdl"), "*.pdl");

    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(
        flags,
        [this, snapshot](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return; // cancelled

            if (!file.hasFileExtension("pdl"))
                file = file.withFileExtension("pdl");

            const auto result = session::pdl::writePatch(snapshot, file);
            const bool ok = result.wasOk();

            const auto opts =
                juce::MessageBoxOptions{}
                    .withIconType(ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon)
                    .withTitle(ok ? "Patch saved" : "Couldn't save patch")
                    .withMessage(ok ? ("Saved to " + file.getFullPathName()) : result.getErrorMessage())
                    .withButton("OK");
            juce::AlertWindow::showAsync(opts, nullptr);
        });
}

void TransportBar::openPatch()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Pedalboard 3");
    fileChooser_ = std::make_unique<juce::FileChooser>("Open Pedalboard Patch", dir, "*.pdl");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser_->launchAsync(
        flags,
        [this](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return; // cancelled

            // Handles both v3 .pdl and legacy Pedalboard2 (.pdl v2) files.
            auto loaded = loadOpenedPatch(file, ctx_.pluginHost);
            if (loaded.patch == nullptr)
            {
                const auto msg =
                    loaded.error.isNotEmpty() ? loaded.error : juce::String("Couldn't read the patch file.");
                const auto opts = juce::MessageBoxOptions{}
                                      .withIconType(juce::MessageBoxIconType::WarningIcon)
                                      .withTitle("Couldn't open patch")
                                      .withMessage(msg)
                                      .withButton("OK");
                juce::AlertWindow::showAsync(opts, nullptr);
                return;
            }

            // Drop nodes that are unsafe to construct (instruments or blacklisted
            // crash-on-construct plugins) BEFORE replacePatch — replacePatch calls
            // createInstance directly (bypassing the scanner's guards). Rebuild a
            // linear chain over the survivors so connections stay valid.
            auto& patch = *loaded.patch;
            const auto countBefore = patch.nodes.size();
            patch.nodes.erase(std::remove_if(patch.nodes.begin(),
                                             patch.nodes.end(),
                                             [this](const session::PatchNode& n)
                                             { return !session::isNodeSafeForChain(ctx_.pluginHost, n); }),
                              patch.nodes.end());
            if (patch.nodes.size() != countBefore)
                patch.connections = session::linearChainConnections(patch.nodes);

            // If Test Tone is on, drop it: the loaded patch becomes the live graph
            // via Session. Clear the toggle so the UI agrees.
            if (toneButton_.getToggleState())
                toneButton_.setToggleState(false, juce::dontSendNotification);

            // CALLER-MANAGED EDITOR LIFECYCLE (D1b / AC-UI-P): close every open
            // editor BEFORE replacePatch destroys the live instances.
            canvas_.closeAllEditors();
            ctx_.session.replacePatch(std::move(patch));
            updateAddPluginEnabled();

            // Surface import notes (Pedalboard2 conversion: dropped state, skipped
            // plugins, etc.) once the chain is in.
            if (!loaded.warnings.isEmpty())
            {
                const auto body = "Imported with notes:\n\n - " + loaded.warnings.joinIntoString("\n - ");
                const auto opts = juce::MessageBoxOptions{}
                                      .withIconType(juce::MessageBoxIconType::InfoIcon)
                                      .withTitle("Patch imported")
                                      .withMessage(body)
                                      .withButton("OK");
                juce::AlertWindow::showAsync(opts, nullptr);
            }
        });
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
