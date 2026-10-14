#include "TransportBar.h"
#include "Alert.h"
#include "DialogFrame.h"
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
#include "session/PreparedGraph.h"
#include "session/Session.h"
#include "SettingsWindow.h"
#include "SignalCanvas.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

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
constexpr int kMenuButtonWidth = 36; // the square folder / gear / plug
constexpr int kAddButtonWidth = 36;  // the square [+]
constexpr int kActivityWidth = 104;  // MIDI and OSC lights
constexpr int kCpuMeterWidth = 168;
constexpr int kMuteButtonWidth = 36;
// The group dividers (#109): a hairline this tall, centred on the bar.
constexpr int kDividerHeight = 20;
constexpr int kDividerGap = 7; // either side of one
// Below this the CPU meter folds away. The fold order down the bar is
// latency, then CPU, then the device badge, and it is a priority order: on a
// narrow window you are playing, not diagnosing, and the patch name is the
// one thing on the bar you actually need to read. Before this, the dividers'
// extra width came out of the patch name and it ellipsised to "3/6 ...".
constexpr int kCpuHideBelow = 1040;
// Below this the latency readout folds away.
constexpr int kLatencyHideBelow = 1180;
// The device badge folds the same way (#90): the output device goes first,
// then the input, then the chip itself, so the bar never has to squeeze the
// patch name down to nothing to make room for a readout.
// Lowered once the menus became icons (#90): that gave back 178 px, most of a
// device name, so each fold happens a window-size earlier than it did.
//
// The input device then went back up from 1200 to 1320 when the master mute
// arrived (#106), because at 1280 the two cannot both fit and the patch name
// was wrapping onto a second line to make room. The mute wins that trade: it
// is the control you reach for when the sound is already wrong, and the input
// device's name is also one click away in the badge's own tooltip.
constexpr int kBadgeDropOutputBelow = 1420;
constexpr int kBadgeDropInputBelow = 1320;
constexpr int kBadgeHideBelow = 1000;
// Below these window widths the transport pill folds its position away, then ♩.
constexpr int kTransportFoldBelow = 1120;
constexpr int kTransportFoldMoreBelow = 960;
// Patch-menu item ids at or above this select patch (id - base).
constexpr int kGoToPatchBase = 1000;
constexpr int kBoardFeatureBase = 500; // the board's feature switches, below kGoToPatchBase

// Build a minimal graph that routes a TestToneProcessor straight to
// the master output, prepared at the current device's sample rate and
// buffer size, or 48k/512 without a device.
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
// Drop a menu under any component. The patch strip is not a Button (#109),
// so the placement lives here and the lit-while-open treatment lives in the
// Button overload below, which is the only thing that needed it.
void showMenuBelow(juce::PopupMenu menu, juce::Component& anchor, std::function<void(int)> onResult = {})
{
    const auto below = anchor.getScreenBounds().withHeight(anchor.getHeight() + Pb3LookAndFeel::kMenuGap);
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(anchor).withTargetScreenArea(below).withMinimumWidth(
            Pb3LookAndFeel::kMenuMinimumWidth),
        std::move(onResult));
}

// A menu button stays lit while its menu is open, and goes back when it
// closes — including when the component is gone by then, hence the
// SafePointer.
void showMenuBelow(juce::PopupMenu menu, juce::Button& button, std::function<void(int)> onResult = {})
{
    button.setColour(juce::TextButton::buttonColourId,
                     tokens::bgCard.overlaidWith(tokens::accentPrimary.withAlpha(0.14f)));
    button.setColour(juce::TextButton::textColourOffId, tokens::accentPrimary);
    button.setColour(juce::ComboBox::outlineColourId, tokens::accentPrimary);

    showMenuBelow(
        std::move(menu),
        static_cast<juce::Component&>(button),
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
    : ctx_(ctx), canvas_(canvas), files_(files), deviceBadge_(ctx), activityLights_(ctx.controlHub),
      latencyReadout_(ctx.audioEngine.getDeviceManager()), cpuMeter_(ctx.audioEngine.getDeviceManager()),
      transportControls_(ctx), patchStrip_(ctx)
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

    fileButton_.setTooltip("File: new, open, save, the patch bank, app mappings, about");
    settingsButton_.setTooltip(
        "Settings: audio devices, MIDI, OSC, Tempo / BPM, Metronome and Looper, and the test tone");
    testTone_.addListener(this);

    addAndMakeVisible(fileButton_);
    addAndMakeVisible(addPluginButton_);
    addAndMakeVisible(deviceBadge_);
    deviceBadge_.onClick = [this] { showAudioDevicesWindow(ctx_, testTone_); };
    deviceBadge_.onLayoutChanged = [this] { resized(); };
    addAndMakeVisible(activityLights_);
    addAndMakeVisible(latencyReadout_);
    latencyReadout_.onLayoutChanged = [this] { resized(); };
    addAndMakeVisible(muteButton_);
    muteButton_.onClick = [this] { ctx_.audioEngine.toggleMasterMute(); };
    ctx_.audioEngine.masterMuteChanged().addChangeListener(this);
    refreshMuteButton();
    addAndMakeVisible(cpuMeter_);
    addAndMakeVisible(transportControls_);
    transportControls_.onLayoutChanged = [this] { resized(); };
    addAndMakeVisible(settingsButton_);
    addAndMakeVisible(pluginsButton_);
    pluginsButton_.setTooltip("Plugins: add a plugin, the plugin list, rescan");
    addPluginButton_.setTooltip("Add plugin");

    patchStrip_.onOpenMenu = [this] { openPatchMenu(); };
    patchStrip_.onLayoutChanged = [this] { resized(); };
    addAndMakeVisible(patchStrip_);

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
    if (source == &ctx_.audioEngine.masterMuteChanged())
    {
        refreshMuteButton();
        return;
    }
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
    }

    // Session state may have transitioned; Add Plugin is enabled only
    // when Idle and Test Tone is off (otherwise the chain isn't the
    // live graph being driven by the engine).
    startToneIfClearLanded();
    updateAddPluginEnabled();
}

void TransportBar::startToneIfClearLanded()
{
    if (!tonePending_)
        return;
    // Idle AND the counter has moved: the clear we asked for has been through
    // the worker and committed. Idle alone would also be true in the gap
    // before the worker starts, which is the window the bug lived in.
    if (ctx_.session.getState() != session::Session::State::Idle ||
        ctx_.session.getStateVersion() <= toneRequestedAtVersion_)
        return;

    tonePending_ = false;
    if (!toneActive_)
        return; // switched off while we waited

    ctx_.audioEngine.loadGraph(buildTestToneGraph(*ctx_.audioEngine.getDeviceManager()));
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));

    // One hairline between each pair of groups (#109). Thirteen controls in
    // four visual treatments read as a row of loose boxes; the dividers are
    // what turn them into menus | device | patch | transport | meters | mute.
    // The demo does the same thing with `h-5 w-px bg-border`.
    const float top = (getHeight() - kDividerHeight) * 0.5f;
    g.setColour(tokens::borderDefault);
    for (const int x : dividerX_)
        g.fillRect(juce::Rectangle<float>(static_cast<float>(x), top, 1.0f, static_cast<float>(kDividerHeight)));
}

void TransportBar::refreshMuteButton()
{
    const bool muted = ctx_.audioEngine.isMasterMuted();
    muteButton_.setIcon(muted ? volumeMutedIcon() : speakerIcon());
    // Red while muted, and the icon changes shape too, so the state does not
    // rest on colour alone. A muted rig that looks live is worse than no mute.
    muteButton_.setColour(juce::TextButton::textColourOffId, muted ? tokens::accentDestructive : tokens::fgMenu);
    muteButton_.setTooltip(muted ? "Muted. Click to let the output through again."
                                 : "Mute the output. The board is untouched, and it stays muted across a patch "
                                   "change.");
}

void TransportBar::resized()
{
    auto bounds = getLocalBounds().reduced(kBarPadding, 6);
    dividerX_.clear();

    // Each group takes a divider's worth of room off the edge it faces, and
    // records where the line goes. Taking the room here rather than painting
    // over the gap is what stops a hairline landing on top of a control when
    // the bar is tight.
    const auto divideRight = [this, &bounds]
    {
        bounds.removeFromRight(kDividerGap);
        dividerX_.push_back(bounds.getRight());
        bounds.removeFromRight(kDividerGap);
    };
    const auto divideLeft = [this, &bounds]
    {
        bounds.removeFromLeft(kDividerGap);
        dividerX_.push_back(bounds.getX());
        bounds.removeFromLeft(kDividerGap);
    };

    // Right cluster: MIDI / OSC lights | latency | CPU | mute.
    muteButton_.setBounds(bounds.removeFromRight(kMuteButtonWidth));
    divideRight();

    const bool latencyOn = getWidth() >= kLatencyHideBelow;
    const bool cpuOn = getWidth() >= kCpuHideBelow;
    latencyReadout_.setVisible(latencyOn);
    cpuMeter_.setVisible(cpuOn);
    const int latencyWidth = latencyOn ? latencyReadout_.preferredWidth() + kButtonGap * 2 : 0;
    const int cpuWidth = cpuOn ? kCpuMeterWidth + kButtonGap : 0;
    auto right = bounds.removeFromRight(kActivityWidth + latencyWidth + cpuWidth);
    activityLights_.setBounds(right.removeFromLeft(kActivityWidth));
    if (latencyOn)
    {
        right.removeFromLeft(kButtonGap * 2);
        latencyReadout_.setBounds(right.removeFromLeft(latencyReadout_.preferredWidth()));
    }
    if (cpuOn)
    {
        right.removeFromLeft(kButtonGap);
        cpuMeter_.setBounds(right.removeFromLeft(kCpuMeterWidth));
    }

    divideRight();

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
        divideRight();
    }

    // Left cluster: folder | gear | plug | +.
    fileButton_.setBounds(bounds.removeFromLeft(kMenuButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    settingsButton_.setBounds(bounds.removeFromLeft(kMenuButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    pluginsButton_.setBounds(bounds.removeFromLeft(kMenuButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    addPluginButton_.setBounds(bounds.removeFromLeft(kAddButtonWidth));
    divideLeft();

    // The device badge, left of the patch strip.
    const bool badgeOn = getWidth() >= kBadgeHideBelow;
    deviceBadge_.setVisible(badgeOn);
    if (badgeOn)
    {
        deviceBadge_.setFoldLevel(getWidth() < kBadgeDropInputBelow ? 2 : getWidth() < kBadgeDropOutputBelow ? 1 : 0);
        deviceBadge_.setBounds(bounds.removeFromLeft(deviceBadge_.preferredWidth()));
        divideLeft();
    }

    // The patch strip takes what it asks for, or what is left if that is
    // less: it is still the first thing on the bar to give way.
    patchStrip_.setBounds(bounds.removeFromLeft(juce::jmin(patchStrip_.preferredWidth(), bounds.getWidth())));
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
                  patchStrip_,
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
    juce::Component::SafePointer<TransportBar> safeThis(this);
    alert::show({"Rename Patch",
                 "Name for patch " + juce::String(index + 1) + ":",
                 {{"Rename", 1, alert::Role::Main}, {"Cancel", 0, alert::Role::Cancel}},
                 {{"name", ctx_.bankDocument.activePatch().name}}},
                [safeThis, index](const alert::Response& response)
                {
                    if (auto* self = safeThis.getComponent(); self != nullptr && response.result == 1)
                        self->ctx_.bankDocument.rename(index, response.field("name"));
                });
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
    juce::Component::SafePointer<TransportBar> safeThis(this);
    alert::show({"Remove Patch",
                 "Remove \"" + doc.activePatch().name + "\" and its " + juce::String(liveNodes) + " " + noun +
                     " from the bank? This can't be undone.",
                 {{"Remove", 1, alert::Role::Main, true}, {"Cancel", 0, alert::Role::Cancel}},
                 {}},
                [safeThis, index](const alert::Response& response)
                {
                    if (auto* self = safeThis.getComponent(); self != nullptr && response.result == 1)
                        self->ctx_.bankDocument.remove(index);
                });
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
        alert::show(alert::message("Crash list couldn't be read",
                                   "The list of plugins that crashed a scan was damaged (" + crashList.error +
                                       "), so none of it was used. It has been set aside as \"" +
                                       setAside.getFileName() + "\"."));
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
    // The mockup's rows and caption header (UI chrome U7); JUCE's are 20 and 22.
    listComponent->getTableListBox().setRowHeight(28);
    listComponent->getTableListBox().setHeaderHeight(28);
    listComponent->setSize(720, 520);

    DialogFrame::launchModal("Plugin List", std::move(listComponent), true);
}

namespace
{

// After a scan: the open board's plugins that weren't in the scanned list
// (placeholders, "Not found: install it, or scan for plugins") and now are get
// loaded. The board keeps its file and unsaved state, and each plugin its saved
// settings (a placeholder holds them). Returns how many were loaded.
int loadNewlyFoundPlugins(AppContext& ctx)
{
    auto& doc = ctx.bankDocument;
    auto bank = doc.snapshotForSave();
    const int before = session::unresolvedNodeCount(bank);
    if (before == 0)
        return 0;
    const int after = session::resolvePluginNodes(bank, ctx.pluginHost);
    if (after >= before)
        return 0;
    session::dropUnsafeNodes(bank, ctx.pluginHost);

    const auto file = doc.currentFile();
    const bool modified = doc.isModified();
    const bool readOnly = doc.isReadOnly();
    doc.loadBank(std::move(bank), file, readOnly);
    doc.restoreIdentity(file, modified, readOnly);
    return before - after;
}

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

        const int loaded = loadNewlyFoundPlugins(ctx_);

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
        // The counts say how many; these say which, and why. ScanResult::error
        // has carried a written-out reason since M5 U6 — "helper died before
        // writing the completion sentinel", "helper produced unparseable
        // output: ..." — and only the tests ever read it (#96). A count alone
        // leaves the user with nothing to act on and nothing to report.
        juce::StringArray failures;
        for (const auto& result : results_)
        {
            const bool failed = result.outcome == Outcome::Crashed || result.outcome == Outcome::TimedOut ||
                                result.outcome == Outcome::NotAPlugin;
            if (!failed)
                continue;
            const auto name = juce::File(result.pluginPath).getFileName();
            failures.add(result.error.isNotEmpty() ? name + " - " + result.error : name);
        }
        if (!failures.isEmpty())
        {
            // Every one goes to the log, whatever the alert has room for.
            for (const auto& line : failures)
                juce::Logger::writeToLog("Pedalboard 3: scan failure - " + line);

            constexpr int kMaxShown = 8;
            summary += "\nWhat failed:\n";
            for (int i = 0; i < juce::jmin(kMaxShown, failures.size()); ++i)
                summary += "  " + failures[i] + "\n";
            if (failures.size() > kMaxShown)
                summary +=
                    "  ...and " + juce::String(failures.size() - kMaxShown) + " more. The log has all of them.\n";
            summary += "\n";
        }

        summary += "On the blacklist: " + juce::String(blacklist.size());
        if (loaded > 0)
            summary += "\n\nLoaded " + juce::String(loaded) + (loaded == 1 ? " plugin" : " plugins") +
                       " this board was missing.";

        alert::show(alert::message(userPressedCancel ? "Scan cancelled" : "Scan complete", summary));

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
        alert::show(alert::message("Plugin scanning unavailable",
                                   "pb3-scanner-helper binary was not found next to the host executable, so "
                                   "out-of-process scanning is disabled. Reinstall Pedalboard 3 (or rebuild so the "
                                   "helper is copied next to the app) to scan for plugins. Already-known plugins "
                                   "remain usable."));
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
    menu.addItem("App Mappings...",
                 [this]
                 {
                     if (onOpenAppMappings)
                         onOpenAppMappings();
                 });
    menu.addSeparator();
    menu.addItem("Test Tone", true, toneActive_, [this] { toggleTestTone(); });
    return menu;
}

void TransportBar::toggleTestTone()
{
    // Setting the Value keeps the Audio Devices dialog's toggle in step, but
    // juce::Value notifies its listeners asynchronously, so valueChanged
    // would not run until the next turn of the message loop. Act now instead:
    // a switch that does nothing until the loop comes round is how this reads
    // as unresponsive, and it is why the headless snapshot tool could not
    // drive it at all (#111). The async notification still arrives afterwards
    // and valueChanged's `want == toneActive_` guard makes it a no-op.
    testTone_ = !toneActive_;
    testTone_.getValueSource().sendChangeMessage(true);
}

void TransportBar::valueChanged(juce::Value&)
{
    // The Test Tone switch. Act only on a real change: a bank swap that already
    // replaced the tone turns the switch off after the fact.
    const bool want = static_cast<bool>(testTone_.getValue());
    if (want == toneActive_)
        return;
    toneActive_ = want;
    if (onTestToneChanged)
        onTestToneChanged();

    if (want)
    {
        // The live graph is about to stop being the active patch: capture its
        // state into the bank slot first, then suspend live capture so a
        // switch/save while the tone plays cannot overwrite the slot with the
        // empty patch clearPatch installs (BankDocument re-enables it on its
        // next swap).
        // Read the state counter BEFORE asking for the clear: "Idle" on its
        // own is ambiguous here, because Session is still Idle for the moment
        // between clearPatch() returning and the worker picking the job up.
        toneRequestedAtVersion_ = ctx_.session.getStateVersion();
        tonePending_ = true;

        ctx_.bankDocument.storeActive();
        ctx_.bankDocument.setLiveCaptureEnabled(false);

        // CALLER-MANAGED EDITOR LIFECYCLE (D1b / AC-UI-P): close
        // every open EditorWindow synchronously BEFORE Session
        // destroys the underlying plugin instances on clearPatch.
        canvas_.closeAllEditors();
        // The tone graph goes in from the Session listener once the clear has
        // landed, not here. Loading it here is what was broken: clearPatch is
        // replacePatch(Patch{}) on the worker, so the empty patch committed a
        // moment later and took the tone with it. The switch said the tone was
        // on and the chain was gone (#111).
        ctx_.session.clearPatch();
        startToneIfClearLanded();
    }
    else
    {
        // Toggle OFF: bring the active bank slot back through Session's
        // worker → commitLoadPatch path (NOT audioEngine.loadGraph(nullptr),
        // which would leave Session::liveGraph_ dangling at the destroyed
        // tone graph and crash the next addPlugin). The slot holds the
        // state captured when the tone went on, so the board comes back as
        // it was. The brief audible silence during the swap is acceptable.
        tonePending_ = false; // switched off before the clear landed: owe nothing
        ctx_.bankDocument.reloadActive();
    }
    updateAddPluginEnabled();
}

} // namespace pedalboard3::ui
