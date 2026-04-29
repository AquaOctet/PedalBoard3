#include "SignalCanvas.h"

#include "ColourScheme.h"
#include "engine/PluginHost.h"
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
constexpr int kCardHeight = 140;
constexpr int kCardSpacingX = 80;
constexpr int kEndpointWidth = 86;
constexpr int kCanvasMarginX = 24;
constexpr int kCanvasMarginY = 24;

constexpr float kCableThickness = 2.5f;
constexpr float kCableMidiThickness = 2.0f;

// Connection channel meanings (Patch.h).
constexpr int kChannelL = 0;
constexpr int kChannelR = 1;
const int kChannelMidi = juce::AudioProcessorGraph::midiChannelIndex;

juce::Colour cableColourFor(int channel)
{
    return channel == kChannelMidi ? tokens::accentInfo : tokens::fgMuted;
}

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
    ctx_.session.addChangeListener(this);

    // First-paint: pull current snapshot so the canvas isn't empty
    // until the next change message arrives.
    changeListenerCallback(&ctx_.session);
}

SignalCanvas::~SignalCanvas()
{
    ctx_.session.removeChangeListener(this);
    editors_.clear();
}

void SignalCanvas::closeAllEditors()
{
    editors_.clear();
}

bool SignalCanvas::isGraphIO(NodeId id) const noexcept
{
    return id == session::graphIO;
}

void SignalCanvas::changeListenerCallback(juce::ChangeBroadcaster*)
{
    auto snaps = ctx_.session.getNodeSnapshots();
    auto patch = ctx_.session.getActivePatch();

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

    // Apply move-button enable state + accent colour from patch metadata.
    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        cards_[i]->setMovableUp(i > 0);
        cards_[i]->setMovableDown(i + 1 < cards_.size());
        const auto* pn = patch.findNode(cards_[i]->getNodeId());
        if (pn != nullptr)
            cards_[i]->setAccentColour(isInstrumentNode(*pn) ? tokens::accentWarning
                                                             : tokens::accentPrimary);
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
    PluginCard::Callbacks cb {
        [this](NodeId id) { onCardBypassToggle(id); },
        [this](NodeId id) { onCardEdit(id); },
        [this](NodeId id) { onCardMoveUp(id); },
        [this](NodeId id) { onCardMoveDown(id); },
        [this](NodeId id) { onCardRemove(id); },
    };
    return std::make_unique<PluginCard>(s, std::move(cb));
}

void SignalCanvas::layoutCards()
{
    const int centreY = juce::jmax(getHeight() / 2, kCanvasMarginY + kCardHeight / 2);
    const int cardY = centreY - kCardHeight / 2;
    int x = kCanvasMarginX;

    inputCardBounds_ = {x, cardY, kEndpointWidth, kCardHeight};
    x += kEndpointWidth + kCardSpacingX;

    idToIndex_.clear();
    idToIndex_.reserve(cards_.size());
    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        cards_[i]->setBounds(x, cardY, kCardWidth, kCardHeight);
        idToIndex_[cards_[i]->getNodeId()] = i;
        x += kCardWidth + kCardSpacingX;
    }

    outputCardBounds_ = {x, cardY, kEndpointWidth, kCardHeight};
    const int rightEdge = outputCardBounds_.getRight() + kCanvasMarginX;

    // Grow canvas width to fit; juce::Viewport handles horizontal
    // scrolling on long chains.
    const int targetWidth = juce::jmax(rightEdge, getParentWidth());
    setSize(targetWidth, juce::jmax(kCardHeight + 2 * kCanvasMarginY, getParentHeight()));

    rebuildCables();
    repaint();
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

void SignalCanvas::rebuildCables()
{
    audioCablesPath_.clear();
    midiCablesPath_.clear();

    auto patch = ctx_.session.getActivePatch();
    for (const auto& c : patch.connections)
    {
        const auto from = portPositionFor(c.src, c.srcChannel, /*asSource*/ true);
        const auto to = portPositionFor(c.dst, c.dstChannel, /*asSource*/ false);
        if (from.isOrigin() || to.isOrigin())
            continue;

        auto& target = (c.srcChannel == kChannelMidi) ? midiCablesPath_ : audioCablesPath_;
        appendCableTo(target,
                      static_cast<float>(from.getX()),
                      static_cast<float>(from.getY()),
                      static_cast<float>(to.getX()),
                      static_cast<float>(to.getY()));
    }
}

void SignalCanvas::resized()
{
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

    // Endpoint card frames.
    auto drawEndpoint = [&g](const juce::Rectangle<int>& r, const juce::String& label) {
        auto rf = r.toFloat().reduced(0.5f);
        g.setColour(tokens::bgCard.withMultipliedAlpha(0.85f));
        g.fillRoundedRectangle(rf, 6.0f);
        g.setColour(tokens::borderDefault);
        g.drawRoundedRectangle(rf, 6.0f, 1.0f);
        g.setColour(tokens::fgMuted);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(label, r, juce::Justification::centred);
    };
    drawEndpoint(inputCardBounds_,  "Input");
    drawEndpoint(outputCardBounds_, "Output");

    // Cables — two paths, two strokes, regardless of N. Audio first
    // (drawn underneath), MIDI on top so it reads even when stacked.
    g.setColour(cableColourFor(kChannelL));
    g.strokePath(audioCablesPath_, juce::PathStrokeType(kCableThickness));
    g.setColour(cableColourFor(kChannelMidi));
    g.strokePath(midiCablesPath_,
                 juce::PathStrokeType(kCableMidiThickness,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
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

void SignalCanvas::onCardMoveUp(NodeId id)
{
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end() || it->second == 0)
        return;
    ctx_.session.moveNode(id, static_cast<int>(it->second) - 1);
}

void SignalCanvas::onCardMoveDown(NodeId id)
{
    auto it = idToIndex_.find(id);
    if (it == idToIndex_.end() || it->second + 1 >= cards_.size())
        return;
    ctx_.session.moveNode(id, static_cast<int>(it->second) + 1);
}

void SignalCanvas::onCardRemove(NodeId id)
{
    // CALLER-MANAGED EDITOR LIFECYCLE (D1b / AC-UI-P): close any open
    // EditorWindow synchronously, on the message thread, BEFORE
    // calling Session::removeNode — Session destroys the underlying
    // BypassableInstance inline before firing the async change
    // message, so a listener-side close would be too late.
    editors_.erase(id);
    ctx_.session.removeNode(id);
}

} // namespace pedalboard3::ui
