// Tests for ui::canvas (ui/CanvasLayout.h), SignalCanvas's geometry: Input and
// Output follow the cards, the group is centred in the visible area, and a
// dropped card knows which cable it landed across.

#include <catch2/catch_test_macros.hpp>

#include "ui/CanvasLayout.h"

#include <optional>
#include <vector>

using namespace pedalboard3::ui;

namespace
{

constexpr canvas::Metrics kM{170, 140, 86, 80, 24};
using Saved = std::vector<std::optional<juce::Point<int>>>;

juce::Rectangle<int> cardAt(const canvas::Layout& l, std::size_t i)
{
    return juce::Rectangle<int>(kM.cardWidth, kM.cardHeight).withPosition(l.modelPositions[i] + l.offset);
}

} // namespace

TEST_CASE("CanvasLayout: an empty board centres Input and Output in the view", "[ui][canvas]")
{
    const auto l = canvas::layout({}, {960, 540}, kM, std::nullopt);
    const auto& in = l.endpoints.input;
    const auto& out = l.endpoints.output;
    REQUIRE(in.getY() == out.getY());
    REQUIRE(out.getX() - in.getRight() == 2 * kM.gap);
    REQUIRE(in.getX() == 960 - out.getRight());
    REQUIRE(in.getCentreY() == 540 / 2);
    REQUIRE(l.canvasSize == juce::Point<int>{960, 540});
}

TEST_CASE("CanvasLayout: Input and Output hug a chain saved far away, and the group is centred", "[ui][canvas]")
{
    // A chain placed while the window was large, far from the origin.
    const Saved saved{juce::Point<int>{1000, 800}, juce::Point<int>{1250, 800}};
    const auto l = canvas::layout(saved, {960, 540}, kM, std::nullopt);
    const auto first = cardAt(l, 0);
    const auto second = cardAt(l, 1);

    REQUIRE(l.endpoints.input.getRight() + kM.gap == first.getX());
    REQUIRE(second.getRight() + kM.gap == l.endpoints.output.getX());
    REQUIRE(l.endpoints.input.getCentreY() == first.getCentreY());
    REQUIRE(l.endpoints.input.getX() == 960 - l.endpoints.output.getRight());
    REQUIRE(first.getCentreY() == 540 / 2);
    REQUIRE(l.canvasSize == juce::Point<int>{960, 540});         // no dead space, nothing to scroll
    REQUIRE(l.modelPositions[0] == juce::Point<int>{1000, 800}); // saved positions are left alone
    REQUIRE(l.autoPlaced.empty());
}

TEST_CASE("CanvasLayout: each endpoint sits on the card its cable runs to", "[ui][canvas]")
{
    // Two cards on different rows. Input belongs to the first in the chain and
    // Output to the last, so they are on different rows too (#107). They used
    // to share the centre of the union, which put both on a row neither card
    // was on.
    const Saved saved{juce::Point<int>{0, 0}, juce::Point<int>{0, 300}};
    const auto l = canvas::layout(saved, {960, 800}, kM, std::nullopt);
    REQUIRE(l.endpoints.input.getY() == cardAt(l, 0).getY());
    REQUIRE(l.endpoints.output.getY() == cardAt(l, 1).getY());
    REQUIRE(l.endpoints.input.getY() != l.endpoints.output.getY());
}

TEST_CASE("CanvasLayout: moving a middle card leaves both endpoints alone", "[ui][canvas]")
{
    // The bug this replaced: the endpoints took the centre of the union of
    // every card, so dragging a card that connects to neither of them moved
    // both — and the user cannot drag an endpoint back.
    const Saved before{juce::Point<int>{0, 0},
                       juce::Point<int>{kM.cardWidth + kM.gap, 0},
                       juce::Point<int>{2 * (kM.cardWidth + kM.gap), 0}};
    const auto l0 = canvas::layout(before, {1600, 800}, kM, std::nullopt);

    // Same board, middle card dragged 100 px up.
    const Saved after{juce::Point<int>{0, 0},
                      juce::Point<int>{kM.cardWidth + kM.gap, -100},
                      juce::Point<int>{2 * (kM.cardWidth + kM.gap), 0}};
    const auto l1 = canvas::layout(after, {1600, 800}, kM, l0.offset);

    REQUIRE(l1.endpoints.input.getY() == l0.endpoints.input.getY());
    REQUIRE(l1.endpoints.output.getY() == l0.endpoints.output.getY());
}

TEST_CASE("CanvasLayout: moving an end card moves only its own endpoint", "[ui][canvas]")
{
    const Saved before{juce::Point<int>{0, 0},
                       juce::Point<int>{kM.cardWidth + kM.gap, 0},
                       juce::Point<int>{2 * (kM.cardWidth + kM.gap), 0}};
    const auto l0 = canvas::layout(before, {1600, 800}, kM, std::nullopt);

    // The first card moves up: Input follows it, Output does not.
    const Saved after{juce::Point<int>{0, -100},
                      juce::Point<int>{kM.cardWidth + kM.gap, 0},
                      juce::Point<int>{2 * (kM.cardWidth + kM.gap), 0}};
    const auto l1 = canvas::layout(after, {1600, 800}, kM, l0.offset);

    REQUIRE(l1.endpoints.input.getY() == cardAt(l1, 0).getY());
    REQUIRE(l1.endpoints.input.getY() < l0.endpoints.input.getY());
    REQUIRE(l1.endpoints.output.getY() == l0.endpoints.output.getY());
}

TEST_CASE("CanvasLayout: a group larger than the view sits a margin from the top-left and scrolls", "[ui][canvas]")
{
    Saved saved;
    for (int i = 0; i < 8; ++i)
        saved.push_back(juce::Point<int>{i * 250, 0});
    const auto l = canvas::layout(saved, {960, 540}, kM, std::nullopt);
    REQUIRE(l.endpoints.input.getX() == kM.margin);
    REQUIRE(l.canvasSize.getX() == l.endpoints.output.getRight() + kM.margin);
    REQUIRE(l.canvasSize.getY() == 540);
}

TEST_CASE("CanvasLayout: a kept offset stops a moved card jumping, but never lets the group past the margin",
          "[ui][canvas]")
{
    const Saved before{juce::Point<int>{0, 0}, juce::Point<int>{250, 0}};
    const auto first = canvas::layout(before, {960, 540}, kM, std::nullopt);

    // Drop the second card 100 px lower: it stays where it was dropped.
    const Saved moved{juce::Point<int>{0, 0}, juce::Point<int>{250, 100}};
    const auto kept = canvas::layout(moved, {960, 540}, kM, first.offset);
    REQUIRE(kept.offset == first.offset);
    REQUIRE(cardAt(kept, 1).getPosition() == juce::Point<int>{250, 100} + first.offset);

    // Dragged far left, past the margin: the offset gives way.
    const Saved farLeft{juce::Point<int>{-2000, 0}, juce::Point<int>{250, 0}};
    const auto clamped = canvas::layout(farLeft, {960, 540}, kM, first.offset);
    REQUIRE(clamped.endpoints.input.getX() == kM.margin);
}

TEST_CASE("CanvasLayout: cards without a saved position continue the row to the right, on its centre", "[ui][canvas]")
{
    const Saved saved{juce::Point<int>{0, 100}, std::nullopt, std::nullopt};
    const auto l = canvas::layout(saved, {1600, 540}, kM, std::nullopt);
    REQUIRE(l.autoPlaced == std::vector<std::size_t>{1, 2});
    REQUIRE(l.modelPositions[1] == juce::Point<int>{kM.cardWidth + kM.gap, 100});
    REQUIRE(l.modelPositions[2] == juce::Point<int>{2 * (kM.cardWidth + kM.gap), 100});

    // A board with nothing saved starts its row at the origin.
    const auto fresh = canvas::layout(Saved{std::nullopt}, {960, 540}, kM, std::nullopt);
    REQUIRE(fresh.modelPositions[0] == juce::Point<int>{0, 0});
}

TEST_CASE("CanvasLayout: a taller card keeps the ports on the row, and the group fits it", "[ui][canvas]")
{
    // Two cards side by side; the second is a processor card 32 px taller.
    const Saved saved{juce::Point<int>{0, 0}, juce::Point<int>{kM.cardWidth + kM.gap, 0}};
    const std::vector<int> heights{kM.cardHeight, kM.cardHeight + 32};
    const auto l = canvas::layout(saved, {1200, 600}, kM, std::nullopt, heights);

    // Input and Output line up with the cards' tops, as if both were standard.
    const auto first = cardAt(l, 0);
    REQUIRE(l.endpoints.input.getY() == first.getY());
    REQUIRE(l.endpoints.output.getY() == first.getY());
    // The group, down to the taller card's bottom, is centred.
    REQUIRE(first.getY() == (600 - (kM.cardHeight + 32)) / 2);

    // An unpositioned tall card continues the row with its top on the others'.
    const Saved withAuto{juce::Point<int>{0, 0}, std::nullopt};
    const auto a = canvas::layout(withAuto, {1200, 600}, kM, std::nullopt, heights);
    REQUIRE(a.modelPositions[1].getY() == a.modelPositions[0].getY());
    REQUIRE(a.modelPositions[1].getX() == kM.cardWidth + kM.gap);
}

// ---------------------------------------------------------------------------
// cableUnderCard: which cable a dropped card lands across (#146).
// ---------------------------------------------------------------------------

namespace
{

// The card a drop aims, placed the way every add path places it: centred on
// the pointer. 170x92 is the standard card, so this one spans x 415..585 and
// y 254..346.
juce::Rectangle<int> droppedCardAt(juce::Point<int> pointer)
{
    return juce::Rectangle<int>(170, 92).withCentre(pointer);
}

juce::Path lineFrom(juce::Point<float> a, juce::Point<float> b)
{
    juce::Path p;
    p.startNewSubPath(a);
    p.lineTo(b);
    return p;
}

} // namespace

TEST_CASE("cableUnderCard: a card dropped on a cable lands across it", "[ui][canvas][drop]")
{
    const auto card = droppedCardAt({500, 300});
    const auto through = lineFrom({300.0f, 300.0f}, {700.0f, 300.0f});

    const auto d = canvas::cableUnderCard(through, card);
    REQUIRE(d.has_value());
    REQUIRE(*d == 0.0f); // straight through the middle
}

TEST_CASE("cableUnderCard: a cable the card does not reach is not a target", "[ui][canvas][drop]")
{
    const auto card = droppedCardAt({500, 300});
    REQUIRE_FALSE(canvas::cableUnderCard(lineFrom({300.0f, 200.0f}, {700.0f, 200.0f}), card).has_value());
}

TEST_CASE("cableUnderCard: the card reaches further along the chain than a pointer did", "[ui][canvas][drop]")
{
    // What changed in #146. The drop used to ask whether the *pointer* was
    // within half a card's height -- 46 px -- of the cable, the same distance
    // in every direction. A card is 170 wide and 92 tall, so sideways it
    // reaches 85 px, and sideways is the direction a chain runs in.
    const auto card = droppedCardAt({500, 300});
    const auto steep = lineFrom({560.0f, 200.0f}, {560.0f, 400.0f}); // 60 px to the right of the pointer

    const auto d = canvas::cableUnderCard(steep, card);
    REQUIRE(d.has_value());
    REQUIRE(*d == 60.0f); // further than the old 46 px tolerance, and still inside the card
}

TEST_CASE("cableUnderCard: the nearer of two cables under one card scores lower", "[ui][canvas][drop]")
{
    // Ranking, which is how a card covering a stacked pair picks one.
    const auto card = droppedCardAt({500, 300});
    const auto near = canvas::cableUnderCard(lineFrom({300.0f, 300.0f}, {700.0f, 300.0f}), card);
    const auto far = canvas::cableUnderCard(lineFrom({300.0f, 330.0f}, {700.0f, 330.0f}), card);

    REQUIRE(near.has_value());
    REQUIRE(far.has_value());
    REQUIRE(*near < *far);
}

TEST_CASE("cableUnderCard: the card's own edge is the boundary", "[ui][canvas][drop]")
{
    // No invented tolerance: the card either covers the cable or it does not.
    const auto card = droppedCardAt({500, 300}); // y 254..346
    REQUIRE(canvas::cableUnderCard(lineFrom({300.0f, 255.0f}, {700.0f, 255.0f}), card).has_value());
    REQUIRE_FALSE(canvas::cableUnderCard(lineFrom({300.0f, 253.0f}, {700.0f, 253.0f}), card).has_value());
}
