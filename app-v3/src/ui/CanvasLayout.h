#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace pedalboard3::ui::canvas
{

// SignalCanvas's geometry, kept free of components so it can be unit-tested.
//
// Cards keep their saved positions (PatchNode x / y) in "model" coordinates;
// the canvas draws them at model + offset. The Input and Output endpoints are
// not saved: they sit one gap left of the leftmost card and one gap right of the
// rightmost, vertically centred on the cards. The whole group (Input, cards,
// Output) is centred in the visible area; when it is larger, it sits `margin`
// from the top-left and the viewport scrolls.
//
// Cards can be taller than `cardHeight` (a built-in processor's card carries
// a control row, M6 U5). Their ports are at the same offsets from the top, so
// everything that lines ports up (auto-placed cards, Input and Output) treats
// every card as `cardHeight` tall. Only the group's size uses real heights.

struct Metrics
{
    int cardWidth;
    int cardHeight; // a standard card; taller ones grow downwards
    int endpointWidth;
    int gap;    // between an endpoint and the nearest card, and between auto-placed cards
    int margin; // between the group and the canvas edge
};

struct Endpoints
{
    juce::Rectangle<int> input;
    juce::Rectangle<int> output;
};

// Input and Output around `cards`, in the same coordinates as the cards. With no
// cards the two sit side by side, two gaps apart, at the origin.
inline Endpoints endpointsAround(const std::vector<juce::Rectangle<int>>& cards, const Metrics& m)
{
    if (cards.empty())
        return {{0, 0, m.endpointWidth, m.cardHeight}, {m.endpointWidth + 2 * m.gap, 0, m.endpointWidth, m.cardHeight}};

    auto bounds = cards.front();
    for (const auto& c : cards)
        bounds = bounds.getUnion(c);
    const int y = bounds.getCentreY() - m.cardHeight / 2;
    return {{bounds.getX() - m.gap - m.endpointWidth, y, m.endpointWidth, m.cardHeight},
            {bounds.getRight() + m.gap, y, m.endpointWidth, m.cardHeight}};
}

struct Layout
{
    std::vector<juce::Point<int>> modelPositions; // per card: its saved position, or its new auto slot
    std::vector<std::size_t> autoPlaced;          // the cards that had no saved position
    juce::Point<int> offset;                      // canvas position = model position + offset
    Endpoints endpoints;                          // canvas coordinates
    juce::Point<int> canvasSize;
};

// `saved`: each card's saved position, or nullopt for a card without one; those
// continue the row to the right of the saved cards, on their centre line.
// `viewport`: the visible area's size. `keepOffset`: the previous layout's
// offset, so a moved card doesn't shift everything; it still gives way as far
// as needed to keep the group `margin` from the top-left. `heights`: each
// card's height, when some are taller than `m.cardHeight` (missing or 0 means
// standard).
inline Layout layout(const std::vector<std::optional<juce::Point<int>>>& saved,
                     juce::Point<int> viewport,
                     const Metrics& m,
                     std::optional<juce::Point<int>> keepOffset,
                     const std::vector<int>& heights = {})
{
    Layout out;
    out.modelPositions.resize(saved.size());
    const auto heightOf = [&heights, &m](std::size_t i)
    { return i < heights.size() && heights[i] > 0 ? heights[i] : m.cardHeight; };

    std::optional<juce::Rectangle<int>> savedBounds;
    for (const auto& p : saved)
    {
        if (!p.has_value())
            continue;
        const juce::Rectangle<int> r{p->getX(), p->getY(), m.cardWidth, m.cardHeight};
        savedBounds = savedBounds.has_value() ? savedBounds->getUnion(r) : r;
    }
    int autoX = savedBounds.has_value() ? savedBounds->getRight() + m.gap : 0;
    const int autoY = savedBounds.has_value() ? savedBounds->getCentreY() - m.cardHeight / 2 : 0;

    std::vector<juce::Rectangle<int>> portRows; // every card at standard height: what ports line up on
    juce::Rectangle<int> fullCards;             // the cards at their real heights
    portRows.reserve(saved.size());
    for (std::size_t i = 0; i < saved.size(); ++i)
    {
        if (saved[i].has_value())
        {
            out.modelPositions[i] = *saved[i];
        }
        else
        {
            out.modelPositions[i] = {autoX, autoY};
            out.autoPlaced.push_back(i);
            autoX += m.cardWidth + m.gap;
        }
        const auto position = out.modelPositions[i];
        portRows.push_back({position.getX(), position.getY(), m.cardWidth, m.cardHeight});
        const juce::Rectangle<int> full{position.getX(), position.getY(), m.cardWidth, heightOf(i)};
        fullCards = i == 0 ? full : fullCards.getUnion(full);
    }

    const auto model = endpointsAround(portRows, m);
    auto group = model.input.getUnion(model.output);
    if (!saved.empty())
        group = group.getUnion(fullCards);

    // Centre the group, or keep the previous offset; either way at least
    // `margin` from the top-left.
    const juce::Point<int> centred{juce::jmax(m.margin, (viewport.getX() - group.getWidth()) / 2) - group.getX(),
                                   juce::jmax(m.margin, (viewport.getY() - group.getHeight()) / 2) - group.getY()};
    out.offset = keepOffset.has_value() ? juce::Point<int>{juce::jmax(keepOffset->getX(), m.margin - group.getX()),
                                                           juce::jmax(keepOffset->getY(), m.margin - group.getY())}
                                        : centred;

    out.endpoints = {model.input + out.offset, model.output + out.offset};
    out.canvasSize = {juce::jmax(viewport.getX(), group.getRight() + out.offset.getX() + m.margin),
                      juce::jmax(viewport.getY(), group.getBottom() + out.offset.getY() + m.margin)};
    return out;
}

} // namespace pedalboard3::ui::canvas
