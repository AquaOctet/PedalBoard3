#include "TransportBar.h"
#include "FeatureSettingsWindows.h"
#include "Pb3LookAndFeel.h"
#include "PluginBrowser.h"
#include "session/FeatureGate.h"

#include "session/AppCommands.h"
#include "session/BoardFiles.h"

#include "AboutWindow.h"
#include "ColourScheme.h"
#include "control/OscServer.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/PluginScanner.h"
#include "engine/processors/TestToneProcessor.h"
#include "FileActions.h"
#include "session/BankDocument.h"
#include "session/EditHistory.h"
#include "session/Preferences.h"
#include "session/Session.h"
#include "SettingsWindow.h"
#include "SignalCanvas.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace pedalboard3::ui
{

namespace
{
constexpr int kBarPadding = 12;
constexpr int kButtonGap = 6;
constexpr int kFileButtonWidth = 64;
constexpr int kAddButtonWidth = 36; // the square [+]
constexpr int kActivityWidth = 104; // MIDI and OSC lights
constexpr int kCpuMeterWidth = 168;
constexpr int kSettingsButtonWidth = 90;
constexpr int kPluginsButtonWidth = 90;
// Patch strip (M3 U5): [<] [n/N  Name] [>].
constexpr int kPatchArrowWidth = 28;
constexpr int kPatchStripGap = 4;
constexpr int kPatchNameMaxWidth = 320;
// Below these window widths the transport pill folds its position away, then ♩.
constexpr int kTransportFoldBelow = 1120;
constexpr int kTransportFoldMoreBelow = 960;
// Patch-menu item ids at or above this select patch (id - base).
constexpr int kGoToPatchBase = 1000;
constexpr int kBoardFeatureBase = 500; // the board's feature switches, below kGoToPatchBase

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

// Opens `menu` under `button` as the mockup does (UI chrome U3): at least the
// menu width, a small gap below the button, and the button tinted teal while
// the menu is open. `onResult` gets the chosen item id, or 0 when dismissed.
void showMenuBelow(juce::PopupMenu menu, juce::Button& button, std::function<void(int)> onResult = {})
{
    button.setColour(juce::TextButton::buttonColourId,
                     tokens::bgCard.overlaidWith(tokens::accentPrimary.withAlpha(0.14f)));
    button.setColour(juce::TextButton::textColourOffId, tokens::accentPrimary);
    button.setColour(juce::ComboBox::outlineColourId, tokens::accentPrimary);

    const auto below = button.getScreenBounds().withHeight(button.getHeight() + Pb3LookAndFeel::kMenuGap);
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(button).withTargetScreenArea(below).withMinimumWidth(
            Pb3LookAndFeel::kMenuMinimumWidth),
        [safeButton = juce::Component::SafePointer<juce::Button>(&button), callback = std::move(onResult)](int result)
        {
            if (auto* opener = safeButton.getComponent())
            {
                opener->removeColour(juce::TextButton::buttonColourId);
                opener->removeColour(juce::TextButton::textColourOffId);
                opener->removeColour(juce::ComboBox::outlineColourId);
            }
            if (callback)
                callback(result);
        });
}

} // namespace

TransportBar::TransportBar(AppContext& ctx, SignalCanvas& canvas, FileActions& files)
    : ctx_(ctx), canvas_(canvas), files_(files), activityLights_(ctx.controlHub),
      cpuMeter_(ctx.audioEngine.getDeviceManager()), transportControls_(ctx)
{
    setOpaque(true);

    // Caller-managed editor lifecycle (D1b / AC-UI-P): every whole-patch swap
    // the bank document issues destroys the live instances, so open editors
    // must close first. Unbound again in the dtor.
    ctx_.bankDocument.beforeSwap = [this] { canvas_.closeAllEditors(); };

    fileButton_.onClick = [this] { openFileMenu(); };
    addPluginButton_.onClick = [this] { openAddPluginMenu(); };
    settingsButton_.onClick = [this] { showMenuBelow(makeSettingsMenu(), settingsButton_); };
    pluginsButton_.onClick = [this] { openPluginsMenu(); };

    settingsButton_.setTooltip(
        "Audio devices, MIDI, OSC, Tempo / BPM, Metronome and Looper settings, and the test tone");
    testTone_.addListener(this);

    addAndMakeVisible(fileButton_);
    addAndMakeVisible(addPluginButton_);
    addAndMakeVisible(activityLights_);
    addAndMakeVisible(cpuMeter_);
    addAndMakeVisible(transportControls_);
    transportControls_.onLayoutChanged = [this] { resized(); };
    addAndMakeVisible(settingsButton_);
    addAndMakeVisible(pluginsButton_);
    pluginsButton_.setTooltip("Add a plugin, the plugin list, rescan");
    addPluginButton_.setTooltip("Add plugin");

    prevPatchButton_.setTooltip("Previous patch");
    nextPatchButton_.setTooltip("Next patch");
    patchButton_.setTooltip(
        "Patch bank: new, duplicate, rename, remove, go to a patch, and the features this board uses");
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
    ctx_.features.addChangeListener(this);
}

TransportBar::~TransportBar()
{
    testTone_.removeListener(this);
    ctx_.bankDocument.beforeSwap = nullptr;
    ctx_.bankDocument.removeChangeListener(this);
    ctx_.session.removeChangeListener(this);
    ctx_.features.removeChangeListener(this);

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
    if (source == &ctx_.features)
    {
        resized(); // the transport pill comes or goes with Tempo / BPM
        return;
    }
    if (source == &ctx_.bankDocument)
    {
        // A swap the bank issued while Test Tone was on (a Program Change, the
        // strip's arrows) replaced the tone graph with a patch, and the
        // document re-enabled live capture as it did so. Clear the toggle to
        // match; a rename or an add leaves capture off and the tone playing.
        if (toneActive_ && ctx_.bankDocument.isLiveCaptureEnabled())
        {
            toneActive_ = false;
            testTone_ = false; // valueChanged() then finds nothing left to do
        }
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

    // Right cluster: MIDI / OSC lights | CPU.
    auto right = bounds.removeFromRight(kActivityWidth + kCpuMeterWidth + kButtonGap);
    activityLights_.setBounds(right.removeFromLeft(kActivityWidth));
    right.removeFromLeft(kButtonGap);
    cpuMeter_.setBounds(right.removeFromLeft(kCpuMeterWidth));

    bounds.removeFromRight(kBarPadding);

    // The transport pill, left of the lights (M6 U4, optional-features U4), only
    // while Tempo / BPM is available. On a narrow window it folds its position
    // away, then its click button; the patch name still gives way first.
    const bool tempoOn = ctx_.features.isAvailable(session::Feature::Tempo);
    transportControls_.setVisible(tempoOn);
    if (tempoOn)
    {
        transportControls_.setFoldLevel(getWidth() < kTransportFoldMoreBelow ? 2
                                        : getWidth() < kTransportFoldBelow   ? 1
                                                                             : 0);
        transportControls_.setBounds(bounds.removeFromRight(transportControls_.preferredWidth()));
        bounds.removeFromRight(kBarPadding);
    }

    // Left cluster: File | Settings | Plugins | +.
    fileButton_.setBounds(bounds.removeFromLeft(kFileButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    settingsButton_.setBounds(bounds.removeFromLeft(kSettingsButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    pluginsButton_.setBounds(bounds.removeFromLeft(kPluginsButtonWidth));
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

juce::PopupMenu TransportBar::makePatchMenu()
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

    // The board's feature switches (optional-features U3, KD4): each feature
    // that's on in Settings, ticked while it's on for this board.
    menu.addSectionHeader("This board uses");
    bool anyEnabled = false;
    for (const auto feature : session::kAllFeatures)
        if (ctx_.features.isEnabled(feature))
        {
            anyEnabled = true;
            menu.addItem(kBoardFeatureBase + static_cast<int>(session::indexOf(feature)),
                         session::featureName(feature),
                         true,
                         ctx_.features.isOnForBoard(feature));
        }
    if (!anyEnabled)
        menu.addItem(juce::PopupMenu::Item("Features you turn on in Settings appear here").setEnabled(false));
    return menu;
}

void TransportBar::openPatchMenu()
{
    showMenuBelow(makePatchMenu(),
                  patchButton_,
                  [this](int choice)
                  {
                      auto& doc = ctx_.bankDocument;
                      if (choice == 1)
                      {
                          session::Patch fresh;
                          fresh.name = "Patch " + juce::String(doc.size() + 1);
                          if (ctx_.features.isAvailable(session::Feature::Tempo))
                              ctx_.preferences.applyNewPatchDefaults(fresh);
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
                      else if (choice >= kBoardFeatureBase &&
                               choice < kBoardFeatureBase + static_cast<int>(session::kFeatureCount))
                      {
                          const auto feature =
                              session::kAllFeatures[static_cast<std::size_t>(choice - kBoardFeatureBase)];
                          ctx_.features.setOnForBoard(feature, !ctx_.features.isOnForBoard(feature));
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
    showMenuBelow(makePluginsMenu(), pluginsButton_);
}

juce::PopupMenu TransportBar::makePluginsMenu()
{
    juce::PopupMenu menu;
    menu.addSubMenu("Add Plugin", makeAddPluginMenu(), canAddPlugin());
    menu.addItem("Plugin Browser",
                 true,
                 isPluginBrowserShown && isPluginBrowserShown(),
                 [this]
                 {
                     if (onTogglePluginBrowser)
                         onTogglePluginBrowser();
                 });
    menu.addItem("Mappings Panel",
                 true,
                 isMappingsPanelShown && isMappingsPanelShown(),
                 [this]
                 {
                     if (onToggleMappingsPanel)
                         onToggleMappingsPanel();
                 });
    menu.addSeparator();
    menu.addItem("Plugin List...", [this] { openPluginList(); });
    menu.addItem("Rescan Plugins", [this] { rescanPlugins(); });
    return menu;
}

void TransportBar::updateAddPluginEnabled()
{
    addPluginButton_.setEnabled(canAddPlugin());
}

bool TransportBar::canAddPlugin() const
{
    // Not while Session is mid-build/swap, AND not while Test Tone is on —
    // the tone graph is not Session-managed, so adding a plugin would either
    // be a no-op (post-fix) or stale-pointer-crash (pre-fix).
    const bool idle = ctx_.session.getState() == session::Session::State::Idle;
    return idle && !toneActive_;
}

juce::PopupMenu TransportBar::makeAddPluginMenu()
{
    // Effects-only filter: v3.0 ships as a live-performance effect rack;
    // instruments are sidestepped per the master-plan scope and to dodge
    // the heavyweight license/auth crashes that hit Current.vst3 and
    // Kontakt-class plugins during construction. See
    // PluginHost::getKnownEffects for the rationale.
    //
    // The mockup's list (UI chrome U3): an Effects section, each plugin with its
    // format on the right, then a Built-in section (the Metronome and Looper
    // only while they're on). Items add through the browser's path, so they
    // join its recents too.
    const auto sections = browser::addPluginSections(browserEntries(ctx_));

    juce::PopupMenu menu;
    menu.addSectionHeader("Effects");

    // A disabled hint stands in when no effects are scanned. The built-ins are
    // still offered below, so an empty plugin list must NOT block adding.
    if (sections.effects.empty())
    {
        const auto allKnown = ctx_.pluginHost.getKnownPlugins().size();
        menu.addItem(juce::PopupMenu::Item(allKnown > 0 ? "(All scanned plugins are instruments)"
                                                        : "(No effects scanned: open Plugin List)")
                         .setEnabled(false));
    }

    const auto addEntries = [this, &menu](const std::vector<browser::Entry>& entries)
    {
        for (const auto& entry : entries)
        {
            juce::PopupMenu::Item item(entry.name);
            item.shortcutKeyDescription = browser::formatTag(entry);
            item.setAction([this, id = entry.id] { addBrowserEntry(ctx_, id, std::nullopt); });
            menu.addItem(std::move(item));
        }
    };
    addEntries(sections.effects);
    if (!sections.builtIn.empty())
    {
        menu.addSectionHeader("Built-in");
        addEntries(sections.builtIn);
    }
    return menu;
}

void TransportBar::openAddPluginMenu()
{
    showMenuBelow(makeAddPluginMenu(), addPluginButton_);
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

    // Read strictly (M5 U7, plan KD8): a damaged list is reported and set aside,
    // never half-applied, and JUCE's dialog below doesn't see it either.
    const auto crashList = ctx_.pluginHost.readCrashList();
    if (crashList.error.isNotEmpty())
    {
        const auto crashedFile = ctx_.pluginHost.getCrashedPluginsFile();
        const auto setAside =
            crashedFile.getParentDirectory().getNonexistentChildFile(crashedFile.getFileName(), ".corrupt", false);
        crashedFile.moveFileTo(setAside);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle("Crash list couldn't be read")
                                         .withMessage("The list of plugins that crashed a scan was damaged (" +
                                                      crashList.error +
                                                      "), so none of it was used. It has been "
                                                      "set aside as \"" +
                                                      setAside.getFileName() + "\".")
                                         .withButton("OK"),
                                     nullptr);
    }
    for (const auto& path : crashList.paths)
    {
        if (!list.getBlacklistedFiles().contains(path))
        {
            list.addToBlacklist(path);
            blacklistChanged = true;
        }
    }

    // Instruments stay in the known list (M5 U6, plan KD8): the dialog shows
    // them, and the effect pickers leave them out (PluginHost::getKnownEffects).

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
        // A host that dies mid-scan leaves this naming the plugin it was on; the
        // next scan skips it once instead of blacklisting it (M5 U6, plan KD8).
        scanner_.setInFlightMarker(ctx_.pluginHost.getScanInFlightFile());
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

        // Why the rest ended as they did (M5 U6, plan KD8).
        using Outcome = engine::PluginScanner::Outcome;
        const auto countOf = [this](Outcome outcome)
        {
            int n = 0;
            for (const auto& result : results_)
                if (result.outcome == outcome)
                    ++n;
            return n;
        };
        const std::pair<Outcome, const char*> reasons[] = {
            {Outcome::Instrument, "Instruments (kept, left out of the effect lists)"},
            {Outcome::Crashed, "Crashed while scanning (blacklisted)"},
            {Outcome::TimedOut, "Timed out (blacklisted)"},
            {Outcome::NotAPlugin, "Not a plugin (blacklisted)"},
            {Outcome::SkippedLastTime, "Skipped: the last scan stopped on it (tried again next time)"},
            {Outcome::Cancelled, "Cancelled (tried again next time)"},
        };
        for (const auto& [outcome, label] : reasons)
            if (const int n = countOf(outcome); n > 0)
                summary += juce::String(label) + ": " + juce::String(n) + "\n";
        summary += "On the blacklist: " + juce::String(blacklist.size());

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
    showMenuBelow(makeFileMenu(), fileButton_);
}

juce::PopupMenu TransportBar::makeFileMenu(juce::ApplicationCommandManager* menuBarCommands)
{
    juce::PopupMenu menu;
    menu.addItem("New", [this] { files_.newBank(); });
    menu.addItem("Open...", [this] { files_.open(); });

    juce::PopupMenu recent;
    for (const auto& file : files_.recentFiles())
        recent.addItem(session::boardfiles::menuLabel(file), [this, file] { files_.openRecent(file); });
    if (recent.getNumItems() > 0)
    {
        recent.addSeparator();
        recent.addItem("Clear Recent Files", [this] { files_.clearRecentFiles(); });
    }
    menu.addSubMenu("Open Recent", recent, recent.getNumItems() > 0);

    const auto* saveCommand = session::findAppCommand(session::appcommands::kSave);
    if (menuBarCommands != nullptr && saveCommand != nullptr)
        menu.addCommandItem(menuBarCommands, saveCommand->juceCommandId, "Save");
    else
        menu.addItem("Save", !ctx_.bankDocument.isReadOnly(), false, [this] { files_.save(); }); // M5 U4
    menu.addItem("Save As...", [this] { files_.saveAs(); });
    menu.addSeparator();
    menu.addItem("Save as Default", [this] { files_.saveAsDefault(); });
    menu.addItem("Reset Default", files_.hasDefault(), false, [this] { files_.resetDefault(); });
    menu.addSeparator();
    if (menuBarCommands == nullptr)
    {
        // On macOS the menu bar has these in Settings and Window.
        menu.addSubMenu("Settings", makeSettingsMenu());
        menu.addItem("Patch Bank",
                     true,
                     isPatchBankShown && isPatchBankShown(),
                     [this]
                     {
                         if (onTogglePatchBank)
                             onTogglePatchBank();
                     });
    }
    menu.addItem("App Mappings...",
                 [this]
                 {
                     if (onOpenAppMappings)
                         onOpenAppMappings();
                 });
    if (menuBarCommands == nullptr)
    {
        // On macOS About is in the app menu.
        menu.addSeparator();
        menu.addItem("About Pedalboard 3", [] { showAboutWindow(); });
    }
    return menu;
}

juce::PopupMenu TransportBar::makeSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addItem("Audio Devices...", [this] { showAudioDevicesWindow(ctx_, testTone_); });
    menu.addItem("MIDI Settings...", [this] { showMidiSettingsWindow(ctx_); });
    menu.addItem("OSC Settings...", [this] { showOscSettingsWindow(ctx_); });
    menu.addItem("Tempo / BPM Settings...", [this] { showTempoSettingsWindow(ctx_); });
    menu.addItem("Metronome Settings...", [this] { showMetronomeSettingsWindow(ctx_); });
    menu.addItem("Looper Settings...", [this] { showLooperSettingsWindow(ctx_); });
    menu.addSeparator();
    menu.addItem("Test Tone", true, toneActive_, [this] { testTone_ = !toneActive_; });
    return menu;
}

void TransportBar::valueChanged(juce::Value&)
{
    // The Test Tone switch. Act only on a real change: a bank swap that already
    // replaced the tone turns the switch off after the fact.
    const bool want = static_cast<bool>(testTone_.getValue());
    if (want == toneActive_)
        return;
    toneActive_ = want;

    if (want)
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
