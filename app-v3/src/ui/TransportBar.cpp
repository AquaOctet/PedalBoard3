#include "TransportBar.h"

#include "ColourScheme.h"
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/PluginScanner.h"
#include "engine/processors/TestToneProcessor.h"
#include "FileActions.h"
#include "session/BankDocument.h"
#include "session/Preferences.h"
#include "session/Session.h"
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
constexpr int kPluginsButtonWidth = 90;
// Patch strip (M3 U5): [<] [n/N  Name] [>].
constexpr int kPatchArrowWidth = 28;
constexpr int kPatchStripGap = 4;
constexpr int kPatchNameMaxWidth = 320;
// Patch-menu item ids at or above this select patch (id - base).
constexpr int kGoToPatchBase = 1000;

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

TransportBar::TransportBar(AppContext& ctx, SignalCanvas& canvas, FileActions& files)
    : ctx_(ctx), canvas_(canvas), files_(files)
{
    setOpaque(true);

    // Caller-managed editor lifecycle (D1b / AC-UI-P): every whole-patch swap
    // the bank document issues destroys the live instances, so open editors
    // must close first. Unbound again in the dtor.
    ctx_.bankDocument.beforeSwap = [this] { canvas_.closeAllEditors(); };

    fileButton_.onClick = [this] { openFileMenu(); };
    addPluginButton_.onClick = [this] { openAddPluginMenu(); };
    settingsButton_.onClick = [this] { openAudioSettings(); };
    pluginsButton_.onClick = [this] { openPluginsMenu(); };

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
    addAndMakeVisible(pluginsButton_);
    pluginsButton_.setTooltip("Plugin list and rescan");

    prevPatchButton_.setTooltip("Previous patch");
    nextPatchButton_.setTooltip("Next patch");
    patchButton_.setTooltip("Patch bank: new, duplicate, rename, remove, or go to a patch");
    prevPatchButton_.onClick = [this] { ctx_.bankDocument.previous(); };
    nextPatchButton_.onClick = [this] { ctx_.bankDocument.next(); };
    patchButton_.onClick = [this] { openPatchMenu(); };
    addAndMakeVisible(prevPatchButton_);
    addAndMakeVisible(patchButton_);
    addAndMakeVisible(nextPatchButton_);

    refreshPatchStrip();
    updateAddPluginEnabled();

    ctx_.bankDocument.addChangeListener(this);
    ctx_.session.addChangeListener(this);
}

TransportBar::~TransportBar()
{
    ctx_.bankDocument.beforeSwap = nullptr;
    ctx_.bankDocument.removeChangeListener(this);
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

void TransportBar::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &ctx_.bankDocument)
    {
        // A swap the bank issued while Test Tone was on (a Program Change, the
        // strip's arrows) replaced the tone graph with a patch, and the
        // document re-enabled live capture as it did so. Clear the toggle to
        // match; a rename or an add leaves capture off and the tone playing.
        if (toneButton_.getToggleState() && ctx_.bankDocument.isLiveCaptureEnabled())
            toneButton_.setToggleState(false, juce::dontSendNotification);
        refreshPatchStrip();
    }

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

    // Right cluster: OSC port | Audio Settings | Test Tone | Plugins.
    auto right = bounds.removeFromRight(kOscLabelWidth + kOscEditorWidth + kSettingsButtonWidth + kToneButtonWidth +
                                        kPluginsButtonWidth + 3 * kButtonGap);
    oscPortLabel_.setBounds(right.removeFromLeft(kOscLabelWidth));
    oscPortEditor_.setBounds(right.removeFromLeft(kOscEditorWidth));
    right.removeFromLeft(kButtonGap);
    settingsButton_.setBounds(right.removeFromLeft(kSettingsButtonWidth));
    right.removeFromLeft(kButtonGap);
    toneButton_.setBounds(right.removeFromLeft(kToneButtonWidth));
    right.removeFromLeft(kButtonGap);
    pluginsButton_.setBounds(right.removeFromLeft(kPluginsButtonWidth));

    bounds.removeFromRight(kBarPadding);

    // Left cluster: File | Add Plugin.
    fileButton_.setBounds(bounds.removeFromLeft(kFileButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    addPluginButton_.setBounds(bounds.removeFromLeft(kAddButtonWidth));
    bounds.removeFromLeft(kBarPadding);

    // Patch strip. The name button takes what is left between the clusters,
    // up to a cap, so it is the part that gives way on a narrow window.
    const int nameWidth =
        juce::jlimit(0, kPatchNameMaxWidth, bounds.getWidth() - 2 * (kPatchArrowWidth + kPatchStripGap));
    prevPatchButton_.setBounds(bounds.removeFromLeft(kPatchArrowWidth));
    bounds.removeFromLeft(kPatchStripGap);
    patchButton_.setBounds(bounds.removeFromLeft(nameWidth));
    bounds.removeFromLeft(kPatchStripGap);
    nextPatchButton_.setBounds(bounds.removeFromLeft(kPatchArrowWidth));
}

void TransportBar::refreshPatchStrip()
{
    const auto& doc = ctx_.bankDocument;
    const int count = doc.size();
    patchButton_.setButtonText(juce::String(doc.activeIndex() + 1) + "/" + juce::String(count) + "   " +
                               doc.activePatch().name);
    prevPatchButton_.setEnabled(count > 1);
    nextPatchButton_.setEnabled(count > 1);
}

void TransportBar::openPatchMenu()
{
    const auto& bank = ctx_.bankDocument.bank();

    juce::PopupMenu menu;
    menu.addItem(1, "New Patch");
    menu.addItem(2, "Duplicate Patch");
    menu.addItem(3, "Rename Patch...");
    menu.addItem(4, "Remove Patch...");
    menu.addSectionHeader("Go to Patch");
    for (int i = 0; i < bank.size(); ++i)
        menu.addItem(kGoToPatchBase + i,
                     juce::String(i + 1) + ".  " + bank.patches[static_cast<size_t>(i)].name,
                     true,
                     i == bank.activeIndex);

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(patchButton_),
                       [this](int choice)
                       {
                           auto& doc = ctx_.bankDocument;
                           if (choice == 1)
                           {
                               session::Patch fresh;
                               fresh.name = "Patch " + juce::String(doc.size() + 1);
                               doc.select(doc.addPatch(std::move(fresh)));
                           }
                           else if (choice == 2)
                           {
                               doc.select(doc.duplicate(doc.activeIndex()));
                           }
                           else if (choice == 3)
                           {
                               promptRenamePatch();
                           }
                           else if (choice == 4)
                           {
                               confirmRemovePatch();
                           }
                           else if (choice >= kGoToPatchBase)
                           {
                               doc.select(choice - kGoToPatchBase);
                           }
                       });
}

void TransportBar::promptRenamePatch()
{
    const int index = ctx_.bankDocument.activeIndex();
    dialog_ = std::make_unique<juce::AlertWindow>(
        "Rename Patch", "Name for patch " + juce::String(index + 1) + ":", juce::MessageBoxIconType::NoIcon);
    dialog_->addTextEditor("name", ctx_.bankDocument.activePatch().name);
    dialog_->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TransportBar> safeThis(this);
    dialog_->enterModalState(true,
                             juce::ModalCallbackFunction::create(
                                 [safeThis, index](int result)
                                 {
                                     auto* self = safeThis.getComponent();
                                     if (self == nullptr || self->dialog_ == nullptr)
                                         return;
                                     if (result == 1)
                                         self->ctx_.bankDocument.rename(index,
                                                                        self->dialog_->getTextEditorContents("name"));
                                     self->dialog_.reset();
                                 }),
                             false);
}

void TransportBar::confirmRemovePatch()
{
    auto& doc = ctx_.bankDocument;
    const int index = doc.activeIndex();

    // The active slot is a cache of the live patch, so count what is live.
    const int liveNodes = ctx_.session.getNodeCount();
    if (liveNodes == 0 && doc.activePatch().nodes.empty())
    {
        doc.remove(index); // nothing to lose
        return;
    }

    const auto noun = liveNodes == 1 ? juce::String("plugin") : juce::String("plugins");
    dialog_ = std::make_unique<juce::AlertWindow>("Remove Patch",
                                                  "Remove \"" + doc.activePatch().name + "\" and its " +
                                                      juce::String(liveNodes) + " " + noun +
                                                      " from the bank? This can't be undone.",
                                                  juce::MessageBoxIconType::WarningIcon);
    dialog_->addButton("Remove", 1);
    dialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TransportBar> safeThis(this);
    dialog_->enterModalState(true,
                             juce::ModalCallbackFunction::create(
                                 [safeThis, index](int result)
                                 {
                                     auto* self = safeThis.getComponent();
                                     if (self == nullptr)
                                         return;
                                     if (result == 1)
                                         self->ctx_.bankDocument.remove(index);
                                     self->dialog_.reset();
                                 }),
                             false);
}

void TransportBar::openPluginsMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Plugin List...");
    menu.addItem(2, "Rescan Plugins");
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(pluginsButton_),
                       [this](int choice)
                       {
                           if (choice == 1)
                               openPluginList();
                           else if (choice == 2)
                               rescanPlugins();
                       });
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
    menu.addItem(1, "New");
    menu.addItem(2, "Open...");
    menu.addItem(3, "Save");
    menu.addItem(4, "Save As...");
    menu.addSeparator();
    menu.addItem(5, "Save as Default");
    menu.addItem(6, "Reset Default", files_.hasDefault());
    menu.addSeparator();
    menu.addItem(7, "App Mappings...");
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(fileButton_),
                       [this](int choice)
                       {
                           if (choice == 1)
                               files_.newBank();
                           else if (choice == 2)
                               files_.open();
                           else if (choice == 3)
                               files_.save();
                           else if (choice == 4)
                               files_.saveAs();
                           else if (choice == 5)
                               files_.saveAsDefault();
                           else if (choice == 6)
                               files_.resetDefault();
                           else if (choice == 7 && onOpenAppMappings)
                               onOpenAppMappings();
                       });
}

void TransportBar::onTestToneToggled()
{
    if (toneButton_.getToggleState())
    {
        // The live graph is about to stop being the active patch: capture its
        // state into the bank slot first, then suspend live capture so a
        // switch/save while the tone plays cannot overwrite the slot with the
        // empty patch clearPatch installs (BankDocument re-enables it on its
        // next swap).
        ctx_.bankDocument.storeActive();
        ctx_.bankDocument.setLiveCaptureEnabled(false);

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
        // Toggle OFF: bring the active bank slot back through Session's
        // worker → commitLoadPatch path (NOT audioEngine.loadGraph(nullptr),
        // which would leave Session::liveGraph_ dangling at the destroyed
        // tone graph and crash the next addPlugin). The slot holds the
        // state captured when the tone went on, so the board comes back as
        // it was. The brief audible silence during the swap is acceptable.
        ctx_.bankDocument.reloadActive();
    }
    updateAddPluginEnabled();
}

} // namespace pedalboard3::ui
