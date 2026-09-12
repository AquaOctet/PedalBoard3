#include "SignalCanvas.h"

#include "CanvasLayout.h"
#include "ColourScheme.h"
#include "control/MidiMapping.h"
#include "engine/PluginHost.h"
#include "engine/processors/VuMeterProcessor.h"
#include "PluginBrowser.h"
#include "ProcessorControls.h"
#include "session/ControlHub.h"
#include "session/EditHistory.h"
#include "session/Patch.h"
#include "session/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <unordered_set>

namespace pedalboard3::ui
{

namespace
{
constexpr int kCardWidth = 170;
constexpr int kCardHeight = PluginCard::kStandardHeight; // the mockup's compact card (UI refresh U7)
constexpr int kCardSpacingX = 80;
constexpr int kEndpointWidth = 86;
constexpr int kCanvasMargin = 24;
// Geometry for ui::canvas::layout (CanvasLayout.h).
constexpr canvas::Metrics kMetrics{kCardWidth, kCardHeight, kEndpointWidth, kCardSpacingX, kCanvasMargin};

constexpr float kCableThickness = 2.5f;
constexpr float kCableMidiThickness = 2.0f;

// Connection channel meanings (Patch.h).
constexpr int kChannelL = 0;
constexpr int kChannelR = 1;
const int kChannelMidi = juce::AudioProcessorGraph::midiChannelIndex;

// Cubic bezier from (x1,y1) to (x2,y2) with horizontal control
// points at the midpoint — gives the curving signal-cable look the
// mockup uses.
void appendCableTo(juce::Path& path, float x1, float y1, float x2, float y2)
{
    path.startNewSubPath(x1, y1);
    const float cx = (x1 + x2) * 0.5f;
    path.cubicTo(cx, y1, cx, y2, x2, y2);
}

bool isInstrumentNode(const session::PatchNode& n) noexcept
{
    return n.desc.isInstrument;
}
} // namespace

SignalCanvas::SignalCanvas(AppContext& ctx) : ctx_(ctx)
{
    setOpaque(true);
    setWantsKeyboardFocus(true); // for Delete-to-remove a selected cable (U4)
    ctx_.session.addChangeListener(this);

    // Undo / redo remove nodes outside the canvas (undoing an add, redoing a
    // remove); their editors must close first, as in onCardRemove.
    ctx_.editHistory.beforeNodeRemoved = [this](NodeId id) { closeEditorsFor(id); };
    startTimerHz(30); // the cards' activity strips and VU meters (UI refresh U7)

    // First-paint: pull current snapshot so the canvas isn't empty
    // until the next change message arrives.
    changeListenerCallback(&ctx_.session);
}

SignalCanvas::~SignalCanvas()
{
    // The mappings dialog is the only thing that arms MIDI learn; destroying
    // it cancels a pending arm, so a MIDI message arriving after teardown can't
    // invoke a dangling callback.
    mappingsWindow_.reset();
    stopTimer();
    ctx_.editHistory.beforeNodeRemoved = nullptr;
    ctx_.session.removeChangeListener(this);
    editors_.clear();
}

void SignalCanvas::closeAllEditors()
{
    editors_.clear();
    closeMappings(); // node ids can be reused across the swap that follows
}

bool SignalCanvas::isGraphIO(NodeId id) const noexcept
{
    return id == session::graphIO;
}

void SignalCanvas::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Whole-patch swap: every processor was replaced, and NodeIds can be
    // reused for different plugins — the per-id editor reconciliation below
    // cannot detect that, leaving an editor open on a destroyed processor
    // (crash-hunt #7). Close everything when the patch generation changes.
    const auto gen = ctx_.session.getPatchGeneration();
    if (gen != lastPatchGeneration_)
    {
        lastPatchGeneration_ = gen;
        editors_.clear();
        // A whole-patch swap can reuse NodeIds for different plugins
        // (crash-hunt #7), so the mappings dialog's node id would no longer
        // mean the plugin it was opened for.
        closeMappings();
    }

    auto snaps = ctx_.session.getNodeSnapshots();
    auto patch = ctx_.session.getActivePatch();
    connections_ = patch.connections; // cached for per-drag-frame cable rebuilds

    bypassedNodes_.clear();
    for (const auto& s : snaps)
        if (s.bypassed)
            bypassedNodes_.insert(s.id);

    // Build new cards_ in snap order, reusing existing PluginCards by
    // NodeId so visual state survives reorder/bypass.
    std::unordered_map<NodeId, std::size_t> oldIndex;
    oldIndex.reserve(cards_.size());
    for (std::size_t i = 0; i < cards_.size(); ++i)
        oldIndex[cards_[i]->getNodeId()] = i;

    std::vector<std::unique_ptr<PluginCard>> next;
    next.reserve(snaps.size());
    std::unordered_set<NodeId> newIds;
    newIds.reserve(snaps.size());

    for (const auto& s : snaps)
    {
        newIds.insert(s.id);
        auto it = oldIndex.find(s.id);
        if (it != oldIndex.end() && cards_[it->second] != nullptr)
        {
            cards_[it->second]->setSnapshot(s);
            next.push_back(std::move(cards_[it->second]));
        }
        else
        {
            next.push_back(makeCard(s));
            addAndMakeVisible(next.back().get());
        }
    }

    // Remaining cards_ entries are nullptr (moved-from) or for departed
    // NodeIds — letting the unique_ptrs fall out of scope destroys them.
    cards_ = std::move(next);

    // Apply accent colour from patch metadata.
    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        const auto* pn = patch.findNode(cards_[i]->getNodeId());
        if (pn != nullptr)
            cards_[i]->setAccentColour(isInstrumentNode(*pn) ? tokens::accentWarning : tokens::accentPrimary);
    }

    // Each card's mapping count (its body line) and the selection (UI refresh
    // U7). A node that went drops its selection and its activity state.
    std::unordered_map<NodeId, int> mappingCounts;
    for (const auto& m : patch.midiMappings)
        if (m.target.kind == control::MidiTargetKind::PluginParameter)
            ++mappingCounts[m.target.node];
    for (const auto& m : patch.oscMappings)
        if (m.target.kind == control::MidiTargetKind::PluginParameter)
            ++mappingCounts[m.target.node];
    if (selectedNode_.has_value() && newIds.find(*selectedNode_) == newIds.end())
    {
        selectedNode_.reset();
        if (onSelectionChanged)
            onSelectionChanged(selectedNode_);
    }
    for (auto it = activityBlinks_.begin(); it != activityBlinks_.end();)
        it = newIds.find(it->first) == newIds.end() ? activityBlinks_.erase(it) : std::next(it);
    for (auto& card : cards_)
    {
        const auto count = mappingCounts.find(card->getNodeId());
        card->setMappingCount(count != mappingCounts.end() ? count->second : 0);
        card->setSelected(selectedNode_ == card->getNodeId());
    }

    // Defensive editor cleanup (D1b): in normal flow the UI handler
    // already erased editors_[id] before invoking the Session mutator,
    // so this loop is usually a no-op. It catches restore-on-launch
    // (App::restoreActivePatchIfAny calls Session::replacePatch with no
    // UI handler in the chain) and any other path that bypasses the UI.
    for (auto it = editors_.begin(); it != editors_.end();)
    {
        if (newIds.find(it->first) == newIds.end())
            it = editors_.erase(it);
        else
            ++it;
    }

    layoutCards();
}

std::unique_ptr<PluginCard> SignalCanvas::makeCard(const PluginCard::NodeSnapshot& s)
{
    PluginCard::Callbacks cb{
        [this](NodeId id) { onCardBypassToggle(id); },
        [this](NodeId id) { onCardEdit(id); },
        [this](NodeId id) { onCardRemove(id); },
        [this](NodeId id) { onCardDragging(id); },
        [this](NodeId id, juce::Point<int> p) { onCardMoved(id, p); },
        [this](NodeId id) { openMappings(id); },
        [this](NodeId id) { setSelectedNode(id); },
        // In-place rename goes through the edit history, so it can be undone (U8).
        [this](NodeId id, juce::String name) { ctx_.editHistory.setNodeCustomName(id, std::move(name)); },
    };
    auto card = std::make_unique<PluginCard>(s, std::move(cb));
    // A built-in processor's control row (M6 U5): Output Toggle's switch, and
    // later the players' and recorders' buttons.
    if (s.builtIn)
        if (auto controls = makeProcessorControls(ctx_, s.internalType, s.id))
        {
            controls->setSize(kCardWidth, ProcessorControls::kHeight);
            card->setControls(std::move(controls));
        }
    return card;
}

void SignalCanvas::layoutCards()
{
    // Cards keep their saved positions (model coordinates); canvas::layout
    // places Input and Output around them and centres the whole group in the
    // visible area (CanvasLayout.h). A card with no saved position gets an auto
    // slot to the right of the others, and that slot is seeded back into the
    // Session as its real position: auto slots are assigned by a running
    // counter over the unpositioned cards only, so without the seed the first
    // drag of any card would shift the rest and pile them up.
    const juce::Point<int> viewport{getParentWidth(), getParentHeight()};

    std::vector<NodeId> ids;
    std::vector<std::optional<juce::Point<int>>> saved;
    std::vector<int> heights; // a processor card is taller (M6 U5)
    ids.reserve(cards_.size());
    saved.reserve(cards_.size());
    heights.reserve(cards_.size());
    for (const auto& card : cards_)
    {
        ids.push_back(card->getNodeId());
        heights.push_back(card->preferredHeight());
        saved.push_back(card->hasStoredPosition() ? std::optional<juce::Point<int>>(card->storedPosition())
                                                  : std::nullopt);
    }

    // Re-centre when the window size or the set of cards changes. A moved card
    // keeps the offset, so nothing jumps away from where it was dropped.
    const bool keepOffset = viewOffset_.has_value() && viewport == lastViewport_ && ids == lastCardIds_;
    const auto result = canvas::layout(saved, viewport, kMetrics, keepOffset ? viewOffset_ : std::nullopt, heights);
    viewOffset_ = result.offset;
    lastViewport_ = viewport;
    lastCardIds_ = std::move(ids);

    idToIndex_.clear();
    idToIndex_.reserve(cards_.size());
    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        cards_[i]->setBounds(juce::Rectangle<int>(kCardWidth, cards_[i]->preferredHeight())
                                 .withPosition(result.modelPositions[i] + result.offset));
        idToIndex_[cards_[i]->getNodeId()] = i;
    }

    // Persist the auto slots (the async change message re-enters layoutCards
    // once, finds every card positioned, and seeds nothing: no loop). Skip
    // while parentless: the constructor's first layout runs before the
    // Viewport adopts the canvas.
    std::vector<session::Session::NodePositionSeed> seeds;
    for (const auto i : result.autoPlaced)
        seeds.push_back({cards_[i]->getNodeId(),
                         static_cast<float>(result.modelPositions[i].getX()),
                         static_cast<float>(result.modelPositions[i].getY())});
    if (!seeds.empty() && getParentComponent() != nullptr)
        ctx_.session.seedNodePositions(seeds);

    inputCardBounds_ = result.endpoints.input;
    outputCardBounds_ = result.endpoints.output;

    // At least the visible area, larger when the group is; the Viewport scrolls.
    setSize(result.canvasSize.getX(), result.canvasSize.getY());

    repaint(); // cables are drawn per-cable in paint() from connections_ + live positions
}

juce::Point<int> SignalCanvas::portPositionFor(NodeId id, int channel, bool asSource) const
{
    if (isGraphIO(id))
    {
        // The graph IO sentinel: as a connection source, the cable
        // emerges from the input endpoint card's right edge; as a
        // destination, it terminates on the output endpoint card's
        // left edge.
        const auto& bounds = asSource ? inputCardBounds_ : outputCardBounds_;
        const int x = asSource ? bounds.getRight() - 8 : bounds.getX() + 8;
        const int rowSpan = bounds.getHeight() / 4;
        int y = bounds.getY() + bounds.getHeight() / 2;
        if (channel == kChannelL)
            y = bounds.getY() + rowSpan;
        else if (channel == kChannelR)
            y = bounds.getY() + 3 * rowSpan;
        // MIDI: middle.
        return {x, y};
    }

    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end())
        return {};

    const auto& card = *cards_[it->second];
    return card.getPosition() + card.getPortCentre(/*isInput*/ !asSource, channel);
}

void SignalCanvas::strokeCable(
    juce::Graphics& g, juce::Point<int> from, juce::Point<int> to, bool midi, bool bypassed, bool selected) const
{
    juce::Path path;
    appendCableTo(path,
                  static_cast<float>(from.getX()),
                  static_cast<float>(from.getY()),
                  static_cast<float>(to.getX()),
                  static_cast<float>(to.getY()));

    const float thickness = selected ? 4.0f : (midi ? kCableMidiThickness : 3.0f);
    const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (selected)
    {
        g.setColour(tokens::accentPrimary);
        g.strokePath(path, stroke);
        return;
    }

    // A bypassed source's cables are dashed and dimmed, so the "off" node reads
    // at a glance (UI refresh U7).
    const auto strokeIt = [&g, &path, &stroke, bypassed]
    {
        if (!bypassed)
        {
            g.strokePath(path, stroke);
            return;
        }
        const float dashes[] = {6.0f, 6.0f};
        juce::Path dashed;
        stroke.createDashedStroke(dashed, path, dashes, 2);
        g.fillPath(dashed);
    };

    if (midi)
    {
        g.setColour(bypassed ? tokens::accentInfo.withAlpha(0.7f) : tokens::accentInfo);
        strokeIt();
        return;
    }

    // Audio: amber -> teal gradient along the cable, the demo's signature look.
    auto start = tokens::accentWarning;
    auto end = tokens::accentPrimary;
    if (bypassed)
    {
        start = start.withAlpha(0.7f);
        end = end.withAlpha(0.7f);
    }
    juce::ColourGradient grad(start, from.toFloat(), end, to.toFloat(), /*isRadial*/ false);
    g.setGradientFill(grad);
    strokeIt();
}

void SignalCanvas::resized()
{
    layoutCards();
}

void SignalCanvas::parentSizeChanged()
{
    // The window, and with it the viewport, changed size: re-centre the board.
    layoutCards();
}

void SignalCanvas::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);

    // Subtle dot grid background — matches the mockup's grid on
    // the SignalFlowCanvas.
    g.setColour(tokens::borderMuted.withAlpha(0.4f));
    constexpr int gridStep = 24;
    auto clip = g.getClipBounds();
    for (int gy = clip.getY() - (clip.getY() % gridStep); gy < clip.getBottom(); gy += gridStep)
        for (int gx = clip.getX() - (clip.getX() % gridStep); gx < clip.getRight(); gx += gridStep)
            g.fillRect(gx, gy, 1, 1);

    // Soft drop shadow behind each card — drawn here (under the cards, which are
    // child components painted on top) so the blurred halo extends past the card
    // bounds for the lifted, modern look.
    for (const auto& card : cards_)
    {
        juce::Path cardPath;
        cardPath.addRoundedRectangle(card->getBounds().toFloat(), 6.0f);
        // The selected card glows teal (UI refresh U7).
        if (selectedNode_ == card->getNodeId())
            juce::DropShadow(tokens::accentPrimary.withAlpha(0.35f), 16, {}).drawForPath(g, cardPath);
        else
            juce::DropShadow(juce::Colours::black.withAlpha(0.35f), 12, {0, 3}).drawForPath(g, cardPath);
    }

    // Endpoint card frames.
    auto drawEndpoint = [&g](const juce::Rectangle<int>& r, const juce::String& label)
    {
        auto rf = r.toFloat().reduced(0.5f);
        g.setColour(tokens::bgCard.withMultipliedAlpha(0.85f));
        g.fillRoundedRectangle(rf, 6.0f);
        g.setColour(tokens::borderDefault);
        g.drawRoundedRectangle(rf, 6.0f, 1.0f);
        g.setColour(tokens::fgMuted);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(label, r, juce::Justification::centred);
    };
    drawEndpoint(inputCardBounds_, "Input");
    drawEndpoint(outputCardBounds_, "Output");

    // Cables — one stroke per connection so each carries its own amber->teal
    // gradient (audio), cyan (MIDI), dashing (bypassed source), or selection
    // highlight. Cheap enough at interactive graph sizes; drawn behind the cards.
    for (const auto& c : connections_)
    {
        const auto from = portPositionFor(c.src, c.srcChannel, /*asSource*/ true);
        const auto to = portPositionFor(c.dst, c.dstChannel, /*asSource*/ false);
        if (from.isOrigin() || to.isOrigin())
            continue;
        const bool midi = (c.srcChannel == kChannelMidi);
        const bool bypassed = (bypassedNodes_.find(c.src) != bypassedNodes_.end());
        const bool selected = (selectedCable_.has_value() && *selectedCable_ == c);
        strokeCable(g, from, to, midi, bypassed, selected);
    }

    // Live connection-drag preview from the anchored port to the cursor.
    if (dragging_)
    {
        const auto anchor = portPositionFor(dragAnchor_.node, dragAnchor_.channel, dragAnchor_.isSource);
        if (!anchor.isOrigin())
        {
            const auto a = anchor.toFloat();
            const auto c = dragCursor_.toFloat();
            juce::Path preview;
            if (dragAnchor_.isSource)
                appendCableTo(preview, a.getX(), a.getY(), c.getX(), c.getY());
            else
                appendCableTo(preview, c.getX(), c.getY(), a.getX(), a.getY());
            g.setColour(tokens::accentPrimary.withAlpha(0.7f));
            g.strokePath(preview, juce::PathStrokeType(kCableThickness, juce::PathStrokeType::curved));
        }
    }
}

// ----- Card delegates --------------------------------------------------------

void SignalCanvas::onCardBypassToggle(NodeId id)
{
    const bool current = false; // Snapshot drives PluginCard's UI; toggle is reflected on next change message.
    (void)current;
    // We don't have the current bypass state here without re-reading;
    // simplest: ask Session for the latest snapshot vector and flip.
    auto snaps = ctx_.session.getNodeSnapshots();
    bool newBypass = false;
    for (const auto& s : snaps)
    {
        if (s.id == id)
        {
            newBypass = !s.bypassed;
            break;
        }
    }
    ctx_.session.setNodeBypass(id, newBypass);
}

void SignalCanvas::onCardEdit(NodeId id)
{
    auto it = editors_.find(id);
    if (it != editors_.end() && it->second != nullptr)
    {
        it->second->toFront(true);
        return;
    }

    auto* plugin = ctx_.session.getHostedPlugin(id);
    if (plugin == nullptr || !plugin->hasEditor())
        return;
    auto* editor = plugin->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    juce::String title = "Plugin";
    auto snaps = ctx_.session.getNodeSnapshots();
    for (const auto& s : snaps)
    {
        if (s.id == id)
        {
            title = s.customName.isNotEmpty() ? s.customName : s.name;
            break;
        }
    }

    auto onClose = [this, id] { editors_.erase(id); };
    editors_[id] = std::make_unique<EditorWindow>(title, editor, std::move(onClose));
}

void SignalCanvas::onCardRemove(NodeId id)
{
    // Through the edit history, so it can be undone. The history calls
    // beforeNodeRemoved (closeEditorsFor) before Session::removeNode.
    ctx_.editHistory.removeNode(id);
}

void SignalCanvas::closeEditorsFor(NodeId id)
{
    // CALLER-MANAGED EDITOR LIFECYCLE (D1b / AC-UI-P): close any open
    // EditorWindow synchronously, on the message thread, BEFORE
    // Session::removeNode — Session destroys the underlying
    // BypassableInstance inline before firing the async change
    // message, so a listener-side close would be too late.
    editors_.erase(id);
    if (mappingsWindow_ != nullptr && mappingsWindow_->getNodeId() == id)
        closeMappings(); // its node is about to go away
}

void SignalCanvas::onCardMoved(NodeId id, juce::Point<int> newPos)
{
    // Save the drop in model coordinates (canvas position minus the view
    // offset). The change message re-runs layoutCards, which keeps the offset
    // for a move, so the card stays where it was dropped; the group only shifts
    // if it would cross the top-left margin.
    const auto model = newPos - viewOffset_.value_or(juce::Point<int>{});
    ctx_.session.setNodePosition(id, static_cast<float>(model.getX()), static_cast<float>(model.getY()));
}

void SignalCanvas::onCardDragging(NodeId)
{
    // The card has already moved itself (ComponentDragger); cables are drawn
    // per-cable in paint() from the live card positions, so a repaint glues them
    // to the moving card. Input and Output follow the chain live; the offset and
    // the canvas size settle when the card is dropped.
    std::vector<juce::Rectangle<int>> live;
    live.reserve(cards_.size());
    for (const auto& card : cards_)
        live.push_back(card->getBounds().withHeight(kCardHeight)); // ports line up at the standard height
    const auto ends = canvas::endpointsAround(live, kMetrics);
    inputCardBounds_ = ends.input;
    outputCardBounds_ = ends.output;
    repaint();
}

void SignalCanvas::openMappings(NodeId id)
{
    if (mappingsWindow_ != nullptr)
    {
        if (mappingsWindow_->getNodeId() == id)
        {
            mappingsWindow_->toFront(true);
            return;
        }
        closeMappings();
    }

    juce::String title = "Plugin";
    for (const auto& s : ctx_.session.getNodeSnapshots())
    {
        if (s.id == id)
        {
            title = s.customName.isNotEmpty() ? s.customName : s.name;
            break;
        }
    }

    // Same contract as EditorWindow: the close button calls back and the
    // canvas drops the window.
    mappingsWindow_ = std::make_unique<MappingsWindow>(ctx_, id, title, [this] { closeMappings(); });
}

void SignalCanvas::closeMappings()
{
    mappingsWindow_.reset();
}

// ----- Port drag-to-connect / cable selection (U4) ---------------------------

SignalCanvas::PortRef SignalCanvas::portAt(juce::Point<int> p) const
{
    constexpr int kPortPickRadius = 16;
    PortRef best;
    int bestDist = kPortPickRadius + 1;

    auto consider = [&](NodeId node, int channel, bool asSource)
    {
        const auto pos = portPositionFor(node, channel, asSource);
        if (pos.isOrigin())
            return;
        const int d = pos.getDistanceFrom(p);
        if (d < bestDist)
        {
            bestDist = d;
            best = {node, channel, asSource, true};
        }
    };

    const int chans[] = {kChannelL, kChannelR, kChannelMidi};

    // graphIO endpoints: Input endpoint emits (asSource), Output endpoint sinks.
    for (const int ch : chans)
    {
        consider(session::graphIO, ch, /*asSource*/ true);
        consider(session::graphIO, ch, /*asSource*/ false);
    }
    // Node ports: outputs (asSource) on the right, inputs (!asSource) on the left.
    for (const auto& card : cards_)
    {
        const NodeId id = card->getNodeId();
        for (const int ch : chans)
        {
            consider(id, ch, /*asSource*/ true);
            consider(id, ch, /*asSource*/ false);
        }
    }
    return best;
}

std::optional<session::PatchConnection> SignalCanvas::cableAt(juce::Point<int> p) const
{
    constexpr float kCablePickTol = 6.0f;
    const auto pf = p.toFloat();
    std::optional<session::PatchConnection> best;
    float bestDist = kCablePickTol;

    for (const auto& c : connections_)
    {
        const auto from = portPositionFor(c.src, c.srcChannel, /*asSource*/ true);
        const auto to = portPositionFor(c.dst, c.dstChannel, /*asSource*/ false);
        if (from.isOrigin() || to.isOrigin())
            continue;

        juce::Path path;
        appendCableTo(path,
                      static_cast<float>(from.getX()),
                      static_cast<float>(from.getY()),
                      static_cast<float>(to.getX()),
                      static_cast<float>(to.getY()));
        juce::Point<float> nearest;
        path.getNearestPoint(pf, nearest);
        const float d = nearest.getDistanceFrom(pf);
        if (d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

std::optional<session::PatchConnection> SignalCanvas::connectionInto(const PortRef& port) const
{
    for (const auto& c : connections_)
        if (c.dst == port.node && c.dstChannel == port.channel)
            return c;
    return std::nullopt;
}

void SignalCanvas::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.getPosition();

    if (const auto port = portAt(p); port.valid)
    {
        selectedCable_.reset();

        // Grabbing a connected destination port detaches its cable and re-drags
        // from the original source, so you can pull a cable off and drop it
        // somewhere else. (A source port instead starts a fresh fan-out drag.)
        if (!port.isSource)
        {
            if (const auto existing = connectionInto(port))
            {
                ctx_.editHistory.removeConnection(
                    existing->src, existing->srcChannel, existing->dst, existing->dstChannel);
                dragAnchor_ = PortRef{existing->src, existing->srcChannel, /*isSource*/ true, /*valid*/ true};
                dragging_ = true;
                dragCursor_ = p;
                repaint();
                return;
            }
        }

        dragging_ = true;
        dragAnchor_ = port;
        dragCursor_ = p;
        repaint();
        return;
    }

    // Background click: clear the card selection, and select a cable under the
    // cursor (or clear the cable selection).
    setSelectedNode(std::nullopt);
    selectedCable_ = cableAt(p);
    repaint();
}

void SignalCanvas::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    dragCursor_ = e.getPosition();
    repaint();
}

void SignalCanvas::mouseUp(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    dragging_ = false;

    const auto target = portAt(e.getPosition());
    // A valid edge needs the two ends to be opposite directions (one source,
    // one dest). Session::addConnection enforces the rest (out->in, same signal,
    // no self/duplicate/cycle).
    if (target.valid && target.isSource != dragAnchor_.isSource)
    {
        const PortRef& src = dragAnchor_.isSource ? dragAnchor_ : target;
        const PortRef& dst = dragAnchor_.isSource ? target : dragAnchor_;
        ctx_.editHistory.addConnection(src.node, src.channel, dst.node, dst.channel);
        // The Session change message rebuilds cables; just clear the preview.
    }
    repaint();
}

bool SignalCanvas::keyPressed(const juce::KeyPress& key)
{
    if (selectedCable_.has_value() && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        const auto c = *selectedCable_;
        selectedCable_.reset();
        ctx_.editHistory.removeConnection(c.src, c.srcChannel, c.dst, c.dstChannel);
        return true;
    }
    return false;
}

bool SignalCanvas::isInterestedInDragSource(const SourceDetails& details)
{
    return browserDragId(details.description).isNotEmpty();
}

void SignalCanvas::itemDropped(const SourceDetails& details)
{
    // Canvas position to model coordinates (what the node saves), with the
    // card centred on the pointer.
    const auto topLeft = details.localPosition - viewOffset_.value_or(juce::Point<int>{}) -
                         juce::Point<int>(kCardWidth / 2, kCardHeight / 2);
    addBrowserEntry(ctx_, browserDragId(details.description), topLeft.toFloat());
}

void SignalCanvas::setSelectedNode(std::optional<NodeId> id)
{
    if (selectedNode_ == id)
        return;
    selectedNode_ = id;
    if (id.has_value())
        selectedCable_.reset();
    for (auto& card : cards_)
        card->setSelected(selectedNode_ == card->getNodeId());
    if (onSelectionChanged)
        onSelectionChanged(selectedNode_);
    repaint();
}

void SignalCanvas::timerCallback()
{
    // Per-plugin activity (a mapped control just moved it) and the VU Meter
    // cards' levels. Processors are read only while Session is idle: during a
    // rebuild the cached pointers can dangle (Test Tone frees the graph right
    // after clearPatch publishes Building).
    const bool idle = ctx_.session.getState() == session::Session::State::Idle;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (auto& card : cards_)
    {
        const auto id = card->getNodeId();
        card->setActivity(activityBlinks_[id].update(ctx_.controlHub.nodeActivity(id), now));
        if (idle && card->isVuMeter())
            if (auto* vu = dynamic_cast<engine::processors::VuMeterProcessor*>(ctx_.session.getHostedPlugin(id)))
                card->setMeterLevels(vu->getLeftLevel(), vu->getRightLevel());
    }
}

} // namespace pedalboard3::ui
