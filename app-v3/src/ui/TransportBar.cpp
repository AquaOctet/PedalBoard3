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
// Between the DELAY column and the CPU / MEM stack (#124). Bigger than the
// gaps inside the right cluster on purpose: DELAY is one narrow column of
// text and CPU / MEM is a block of meters, and at kButtonGap they ran
// together into one unreadable strip.
constexpr int kDelayMetersGap = 18;

// How long a fold step takes to reveal or conceal (#115). Long enough to
// read as movement rather than a jump, short enough that dragging a window
// edge never feels like the bar is lagging behind it.
constexpr double kRevealMs = 200.0;
constexpr int kMenuButtonWidth = 36; // the square folder / gear / plug
constexpr int kActivityWidth = 104;  // MIDI and OSC lights
constexpr int kSystemMetersWidth = 168;
constexpr int kMuteButtonWidth = 36;
// The group dividers (#109): a hairline this tall, centred on the bar.
constexpr int kDividerHeight = 20;
constexpr int kDividerGap = 7; // either side of one
constexpr int kIoMonitorWidth = IoMonitor::kPreferredWidth;

// How the bar gives way (#116).
//
// It used to fold on absolute window widths: below 1420 the badge dropped the
// output device, below 1180 the latency went, and so on. The trouble with a
// threshold is that it does not know what else is on the bar. Between the
// patch strip and the right-hand cluster there is slack -- often a couple of
// hundred pixels of it -- and a threshold spends none of that before it
// starts taking things away. Drag a 1500 px window in by sixty pixels and the
// output device name disappeared while the empty space it could have come out
// of was still sitting there. That is the complaint this replaces: "there is
// a lot of white space between the top patch and the midi/osc sensor, and
// instead of using that when shrinking, it decides to collapse other things".
//
// So the bar now prices itself. Everything is laid out at its full size, the
// shortfall against the window is measured, and the ladder below is walked
// only as far as covering that shortfall needs. With slack in hand, nothing
// folds at all.
//
// The ladder is a priority order, cheapest loss first. Diagnostics give way
// before identity, and identity before control:
enum class Fold
{
    LatencyAway,  // a number that moves only when the device does
    CpuAway,      // the CPU and MEM stack, next least urgent while playing
    BadgePairs,   // "[1,2]" twice -- the detail, not the device (#114)
    IoMetersAway, // IN / OUT: still diagnostics, but live ones
    BadgeOutput,  // now we are losing which device you are on
    TransportPos, // the position readout inside the pill
    BadgeInput,
    TransportClick,
    BadgeAway, // the driver chip goes too
    ActivityAway,
    Count
};
constexpr int kFoldSteps = static_cast<int>(Fold::Count);
// Patch-menu item ids at or above this select patch (id - base).
constexpr int kGoToPatchBase = 1000;
constexpr int kBoardFeatureBase = 500; // the board's feature switches, below kGoToPatchBase

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
      ioMonitor_(ctx.audioEngine), latencyReadout_(ctx.audioEngine.getDeviceManager()),
      systemMeters_(ctx.audioEngine.getDeviceManager()), transportControls_(ctx)
{
    setOpaque(true);

    // Caller-managed editor lifecycle (D1b / AC-UI-P): every whole-patch swap
    // the bank document issues destroys the live instances, so open editors
    // must close first. Unbound again in the dtor.
    ctx_.bankDocument.beforeSwap = [this] { canvas_.closeAllEditors(); };

    fileButton_.onClick = [this] { openFileMenu(); };
    settingsButton_.onClick = [this] { showMenuBelow(makeSettingsMenu(), settingsButton_); };
    pluginsButton_.onClick = [this] { openPluginsMenu(); };

    fileButton_.setTooltip("File: new, open, save, the patch bank, app mappings, about");
    settingsButton_.setTooltip(
        "Settings: audio devices, MIDI, OSC, Tempo / BPM, Metronome and Looper, and the test tone");
    testTone_.addListener(this);

    addAndMakeVisible(fileButton_);
    addAndMakeVisible(deviceBadge_);
    deviceBadge_.onClick = [this] { showAudioDevicesWindow(ctx_, testTone_); };
    deviceBadge_.onLayoutChanged = [this]
    {
        // A device with a longer name makes the bar wider at full size, and
        // the window's minimum follows it (#116).
        resized();
        if (onNaturalWidthChanged)
            onNaturalWidthChanged();
    };
    addAndMakeVisible(activityLights_);
    addAndMakeVisible(ioMonitor_);
    addAndMakeVisible(latencyReadout_);
    latencyReadout_.onLayoutChanged = [this]
    {
        resized();
        if (onNaturalWidthChanged)
            onNaturalWidthChanged();
    };
    addAndMakeVisible(muteButton_);
    muteButton_.onClick = [this] { ctx_.audioEngine.toggleMasterMute(); };
    ctx_.audioEngine.masterMuteChanged().addChangeListener(this);
    refreshMuteButton();
    addAndMakeVisible(systemMeters_);
    addAndMakeVisible(transportControls_);
    transportControls_.onLayoutChanged = [this] { resized(); };
    addAndMakeVisible(settingsButton_);
    addAndMakeVisible(pluginsButton_);
    pluginsButton_.setTooltip("Plugins: add a plugin, the plugin list, rescan");

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
        // The transport pill comes or goes with Tempo / BPM, which changes
        // what the bar needs at full size.
        resized();
        if (onNaturalWidthChanged)
            onNaturalWidthChanged();
        return;
    }
    // Session state may have transitioned; Add Plugin follows it. The tone
    // is no longer part of that question: it is a master stage, so the board
    // stays live and editable while it plays.
    updateAddPluginEnabled();
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

int TransportBar::naturalWidth() const
{
    // Everything on the bar at full size, with the patch strip at its
    // narrowest. That last part matters: the strip's preferred width follows
    // the patch NAME, so pricing it at preferred would make the window's
    // minimum size jump about every time you renamed a patch.
    int w = kBarPadding * 2;

    // Left cluster, always present.
    w += kMenuButtonWidth * 3 + kButtonGap * 2;

    // The badge is centred, so it needs clear air on both sides of itself
    // rather than a divider on one.
    w += kButtonGap * 2 + deviceBadge_.widthAtFold(0);

    if (ctx_.features.isAvailable(session::Feature::Tempo))
        w += kDividerGap * 2 + transportControls_.widthAtFold(0);

    w += kDividerGap * 2; // ... | right cluster
    w += kActivityWidth + kButtonGap + kIoMonitorWidth;
    w += kButtonGap * 2 + latencyReadout_.preferredWidth();
    w += kDelayMetersGap + kSystemMetersWidth;
    w += kDividerGap * 2 + kMuteButtonWidth;

    return w;
}

// Where the ladder says each part should end up at this width. Pure: it
// reads only full widths, never the animated ones (see Reveal).
TransportBar::Reveal TransportBar::foldTargetsFor(int width) const
{
    // How much we are over, if at all. Everything below is about spending
    // that number down; with it at or below zero the bar lays out whole.
    const int deficit = naturalWidth() - width;

    // Walk the ladder only as far as covering the shortfall needs, taking
    // each step's saving off what is left. A step that saves little is still
    // taken in its place rather than skipped for a fatter one further down:
    // the order is a statement about what is worth losing first, not an
    // optimisation problem.
    const auto savingOf = [this](Fold step)
    {
        switch (step)
        {
        case Fold::LatencyAway:
            return latencyReadout_.preferredWidth() + kButtonGap * 2;
        case Fold::CpuAway:
            return kSystemMetersWidth + kDelayMetersGap;
        case Fold::BadgePairs:
            return deviceBadge_.widthAtFold(0) - deviceBadge_.widthAtFold(1);
        case Fold::IoMetersAway:
            return kIoMonitorWidth + kButtonGap;
        case Fold::BadgeOutput:
            return deviceBadge_.widthAtFold(1) - deviceBadge_.widthAtFold(2);
        case Fold::TransportPos:
            return ctx_.features.isAvailable(session::Feature::Tempo)
                       ? transportControls_.widthAtFold(0) - transportControls_.widthAtFold(1)
                       : 0;
        case Fold::BadgeInput:
            return deviceBadge_.widthAtFold(2) - deviceBadge_.widthAtFold(3);
        case Fold::TransportClick:
            return ctx_.features.isAvailable(session::Feature::Tempo)
                       ? transportControls_.widthAtFold(1) - transportControls_.widthAtFold(2)
                       : 0;
        case Fold::BadgeAway:
            return deviceBadge_.widthAtFold(3) + kButtonGap * 2;
        case Fold::ActivityAway:
            return kActivityWidth;
        case Fold::Count:
            break;
        }
        return 0;
    };

    int folded = 0;
    for (int remaining = deficit; remaining > 0 && folded < kFoldSteps; ++folded)
        remaining -= savingOf(static_cast<Fold>(folded));

    const auto applied = [folded](Fold step) { return folded > static_cast<int>(step); };

    const int badgeFold = applied(Fold::BadgeInput)    ? 3
                          : applied(Fold::BadgeOutput) ? 2
                          : applied(Fold::BadgePairs)  ? 1
                                                       : 0;
    const int transportFold = applied(Fold::TransportClick) ? 2 : applied(Fold::TransportPos) ? 1 : 0;

    Reveal out;
    out.badgeFold = badgeFold;
    out.transportFold = transportFold;
    out.latency = applied(Fold::LatencyAway) ? 0.0f : 1.0f;
    out.cpu = applied(Fold::CpuAway) ? 0.0f : 1.0f;
    out.io = applied(Fold::IoMetersAway) ? 0.0f : 1.0f;
    out.activity = applied(Fold::ActivityAway) ? 0.0f : 1.0f;
    out.badgeWidth = applied(Fold::BadgeAway) ? 0.0f : static_cast<float>(deviceBadge_.widthAtFold(badgeFold));
    out.transportWidth = ctx_.features.isAvailable(session::Feature::Tempo)
                             ? static_cast<float>(transportControls_.widthAtFold(transportFold))
                             : 0.0f;
    return out;
}

void TransportBar::startReveal()
{
    revealFrom_ = shown_;
    // The levels switch at once; only the boxes ease. See Reveal.
    shown_.badgeFold = target_.badgeFold;
    shown_.transportFold = target_.transportFold;

    // A retarget mid-flight starts fresh from wherever we got to, so the
    // old animation comes off the updater first. resized() is re-entrant
    // from DeviceBadge, LatencyReadout and TransportControls, so this
    // happens whenever a device or a latency changes while folding.
    if (revealAnimator_.has_value())
        animatorUpdater_.removeAnimator(*revealAnimator_);

    revealAnimator_ =
        juce::ValueAnimatorBuilder{}
            .withDurationMs(kRevealMs)
            .withEasing(juce::Easings::createEaseOut())
            .withValueChangedCallback(
                [this](float progress)
                {
                    const auto mix = [progress](float from, float to) { return from + (to - from) * progress; };
                    shown_.latency = mix(revealFrom_.latency, target_.latency);
                    shown_.cpu = mix(revealFrom_.cpu, target_.cpu);
                    shown_.io = mix(revealFrom_.io, target_.io);
                    shown_.activity = mix(revealFrom_.activity, target_.activity);
                    shown_.badgeWidth = mix(revealFrom_.badgeWidth, target_.badgeWidth);
                    shown_.transportWidth = mix(revealFrom_.transportWidth, target_.transportWidth);
                    applyLayout(shown_);
                    repaint(); // the dividers are painted, not components
                })
            .withOnCompleteCallback(
                [this]
                {
                    shown_ = target_;
                    applyLayout(shown_);
                    repaint();
                })
            .build();
    animatorUpdater_.addAnimator(*revealAnimator_);
    revealAnimator_->start();
}

void TransportBar::resized()
{
    const auto targets = foldTargetsFor(getWidth());

    // Snap, don't ease, when nobody can see it: the first layout, and the
    // snapshot rig, which renders offscreen and measures the result in the
    // same breath. An animation there would be measured at frame zero.
    const bool settleNow = !everLaidOut_ || !isShowing();
    everLaidOut_ = true;

    if (targets != target_)
    {
        target_ = targets;
        if (settleNow)
            shown_ = targets;
        else
            startReveal();
    }
    else if (settleNow)
    {
        shown_ = targets;
    }

    // Always lay out for the current width, animating or not: the bar has
    // to track the window edge while it is dragged. Only the fold steps are
    // eased, and they are eased by `shown_`, not by withholding the layout.
    applyLayout(shown_);
}

void TransportBar::applyLayout(const Reveal& reveal)
{
    dividerX_.clear();

    const bool latencyOn = reveal.latency > 0.0f;
    const bool cpuOn = reveal.cpu > 0.0f;
    const bool ioMetersOn = reveal.io > 0.0f;
    const bool activityOn = reveal.activity > 0.0f;
    const bool badgeOn = reveal.badgeWidth > 0.0f;

    auto bounds = getLocalBounds().reduced(kBarPadding, 6);

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
    // Right cluster: MIDI / OSC lights | input | latency | CPU | mute.
    muteButton_.setBounds(bounds.removeFromRight(kMuteButtonWidth));
    divideRight();

    latencyReadout_.setVisible(latencyOn);
    systemMeters_.setVisible(cpuOn);
    ioMonitor_.setVisible(ioMetersOn);
    activityLights_.setVisible(activityOn);

    // A part at presence p is p of its width and p of its opacity, so it
    // narrows and fades together while its neighbours slide into the room
    // it gives up. At p == 1 every number below is the whole width it
    // always was, which is what keeps the settled layout identical.
    const auto scaled = [](int full, float presence) { return juce::roundToInt(static_cast<float>(full) * presence); };

    const int activityWidth = scaled(kActivityWidth, reveal.activity);
    const int inputWidth = scaled(kIoMonitorWidth + kButtonGap, reveal.io);
    const int latencyWidth = scaled(latencyReadout_.preferredWidth() + kButtonGap * 2, reveal.latency);
    const int cpuWidth = scaled(kSystemMetersWidth + kDelayMetersGap, reveal.cpu);
    auto right = bounds.removeFromRight(activityWidth + inputWidth + latencyWidth + cpuWidth);
    if (activityOn)
    {
        activityLights_.setAlpha(reveal.activity);
        activityLights_.setBounds(right.removeFromLeft(activityWidth));
    }
    if (ioMetersOn)
    {
        auto cell = right.removeFromLeft(inputWidth);
        cell.removeFromLeft(scaled(kButtonGap, reveal.io));
        ioMonitor_.setAlpha(reveal.io);
        ioMonitor_.setBounds(cell);
    }
    if (latencyOn)
    {
        auto cell = right.removeFromLeft(latencyWidth);
        cell.removeFromLeft(scaled(kButtonGap * 2, reveal.latency));
        latencyReadout_.setAlpha(reveal.latency);
        latencyReadout_.setBounds(cell);
    }
    if (cpuOn)
    {
        auto cell = right.removeFromLeft(cpuWidth);
        cell.removeFromLeft(scaled(kDelayMetersGap, reveal.cpu));
        systemMeters_.setAlpha(reveal.cpu);
        systemMeters_.setBounds(cell);
    }

    divideRight();

    // The transport pill, left of the lights (M6 U4, optional-features U4),
    // only while Tempo / BPM is available.
    const int transportWidth = juce::roundToInt(reveal.transportWidth);
    transportControls_.setVisible(transportWidth > 0);
    if (transportWidth > 0)
    {
        transportControls_.setFoldLevel(reveal.transportFold);
        transportControls_.setBounds(bounds.removeFromRight(transportWidth));
        divideRight();
    }

    // Left cluster: folder | gear | plug | +.
    fileButton_.setBounds(bounds.removeFromLeft(kMenuButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    settingsButton_.setBounds(bounds.removeFromLeft(kMenuButtonWidth));
    bounds.removeFromLeft(kButtonGap);
    pluginsButton_.setBounds(bounds.removeFromLeft(kMenuButtonWidth));

    // The device badge, centred rather than packed against the left cluster
    // (#122).
    //
    // Left-anchored, it moved every time it folded or the device name
    // changed length, so the thing you glance at to check what you are
    // plugged into was never twice in the same place. Centred, a fold takes
    // a little off each end and the middle of the badge stays put, which is
    // the half of it your eye is on.
    //
    // Centred in the SPACE IT HAS, not on the window, and that is a real
    // difference rather than a shortcut. The left cluster is three 36 px
    // buttons; the right is the transport pill, four meters and the mute,
    // around 500 px of it. Window-centring the badge at 1781 px would want
    // it at x=676, and the free run between the clusters ends at 972 -- it
    // would sit underneath the transport pill at every width this app is
    // ever opened at, and the clamp that saved it from that put it back
    // hard against the left cluster, which is where it started.
    //
    // Centred between the clusters is also what reads as centred: the right
    // cluster is a block of controls, not background, so the eye centres the
    // badge on the gap rather than on the window.
    deviceBadge_.setVisible(badgeOn);
    if (badgeOn)
    {
        deviceBadge_.setFoldLevel(reveal.badgeFold);
        const int want = juce::roundToInt(reveal.badgeWidth);
        // Opaque at every real fold level; it only fades over the last
        // stretch, on its way out past its narrowest one.
        const auto narrowest = static_cast<float>(juce::jmax(1, deviceBadge_.widthAtFold(3)));
        deviceBadge_.setAlpha(juce::jlimit(0.0f, 1.0f, reveal.badgeWidth / narrowest));
        freeRegion_ = {bounds.getX(), bounds.getRight()};
        auto cell = bounds.withSizeKeepingCentre(juce::jmin(want, bounds.getWidth()), bounds.getHeight());
        deviceBadge_.setBounds(cell);

        // Both sides of it are slack, so the fold ladder's measure of "room
        // still unspent" is the whole gap, not just the half to its right.
        slack_ = juce::jmax(0, bounds.getWidth() - want);
        bounds = {};
    }

    // With no badge there is nothing in the middle at all, so the whole gap
    // is slack. (With one, it was recorded above, around the badge.)
    if (!badgeOn)
        slack_ = bounds.getWidth();
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
    // Nothing on the bar is gated on this any more -- the [+] is gone and
    // the Plugins menu is rebuilt each time it opens, so it reads
    // canAddPlugin() itself. Kept as the single place that decides.
    (void)canAddPlugin();
}

bool TransportBar::canAddPlugin() const
{
    // Session Idle, and that is all. The tone used to disable this too,
    // because the tone graph was not Session-managed and an add would have
    // gone to a graph nobody was driving. A master-stage tone leaves the
    // board live, so there is nothing to stop you editing while it plays.
    return ctx_.session.getState() == session::Session::State::Idle;
}

juce::PopupMenu TransportBar::makeAddPluginMenu(std::optional<juce::Point<float>> position)
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

    const auto addEntries = [this, &menu, position](const std::vector<browser::Entry>& entries)
    {
        for (const auto& entry : entries)
        {
            juce::PopupMenu::Item item(entry.name);
            item.shortcutKeyDescription = browser::formatTag(entry);
            item.setAction([this, id = entry.id, position] { addBrowserEntry(ctx_, id, position); });
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

void TransportBar::openAddPluginMenuAt(juce::Point<float> position)
{
    if (!canAddPlugin())
        return;

    // At the mouse, not under a button: this one is opened by right-clicking
    // the board, and the card is going to land where the click was.
    makeAddPluginMenu(position).showMenuAsync(
        juce::PopupMenu::Options{}.withMinimumWidth(Pb3LookAndFeel::kMenuMinimumWidth));
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
    // The board's feature switches (optional-features U3, KD4): each feature
    // that is on in Settings, ticked while it is on for THIS board.
    //
    // They lived in the patch strip's menu until #120 removed the strip, and
    // Settings is where they should always have been -- the same menu turns
    // the features on app-wide, so turning one on for the board it is for is
    // two items apart rather than in a different control entirely.
    menu.addSectionHeader("This board uses");
    bool anyEnabled = false;
    for (const auto feature : session::kAllFeatures)
        if (ctx_.features.isEnabled(feature))
        {
            anyEnabled = true;
            const auto f = feature;
            menu.addItem(session::featureName(f),
                         true,
                         ctx_.features.isOnForBoard(f),
                         [this, f] { ctx_.features.setOnForBoard(f, !ctx_.features.isOnForBoard(f)); });
        }
    if (!anyEnabled)
        menu.addItem(juce::PopupMenu::Item("Features you turn on above appear here").setEnabled(false));

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

bool TransportBar::isTestToneLive() const noexcept
{
    return ctx_.audioEngine.isTestToneEnabled();
}

void TransportBar::valueChanged(juce::Value&)
{
    // The Test Tone switch. Act only on a real change: the Settings menu and
    // the Audio Devices dialog share this Value, so turning it on in one
    // place notifies the other.
    const bool want = static_cast<bool>(testTone_.getValue());
    if (want == toneActive_)
        return;
    toneActive_ = want;

    // That is the whole of it now. The tone is a master stage in the runtime
    // (#111), so there is no graph to build, no patch to clear and wait for,
    // no bank slot to stash the board in, and no editors to close first --
    // the board keeps running untouched behind the tone and comes back
    // without being rebuilt. Everything this function used to do was there
    // to survive a design in which a diagnostic deleted your rig.
    ctx_.audioEngine.setTestToneEnabled(want);

    if (onTestToneChanged)
        onTestToneChanged();
}

} // namespace pedalboard3::ui
