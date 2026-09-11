// Tests for ui::canvas (ui/CanvasLayout.h), SignalCanvas's geometry: Input and
// Output follow the cards, and the group is centred in the visible area.

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

TEST_CASE("CanvasLayout: Input and Output sit on the vertical centre of cards on several rows", "[ui][canvas]")
{
    const Saved saved{juce::Point<int>{0, 0}, juce::Point<int>{0, 300}};
    const auto l = canvas::layout(saved, {960, 800}, kM, std::nullopt);
    const auto cards = cardAt(l, 0).getUnion(cardAt(l, 1));
    REQUIRE(l.endpoints.input.getCentreY() == cards.getCentreY());
    REQUIRE(l.endpoints.output.getCentreY() == cards.getCentreY());
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
