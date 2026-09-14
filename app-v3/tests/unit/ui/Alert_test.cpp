// Tests for the alert's button order and roles (ui/Alert.h; UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U5 / KD4): the main action
// last, the cancel before it, anything else first; Enter runs the main action
// and Escape cancels.

#include <catch2/catch_test_macros.hpp>

#include "ui/Alert.h"

#include <vector>

using namespace pedalboard3::ui;
using alert::Role;

namespace
{

juce::StringArray textsOf(const std::vector<alert::Button>& buttons)
{
    juce::StringArray texts;
    for (const auto& b : buttons)
        texts.add(b.text);
    return texts;
}

} // namespace

TEST_CASE("Alert: three buttons read Don't Save, Cancel, Save, with Save the default", "[ui][alert]")
{
    const std::vector<alert::Button> buttons{
        {"Save", 1, Role::Main}, {"Don't Save", 2, Role::Other}, {"Cancel", 0, Role::Cancel}};

    REQUIRE(textsOf(alert::orderedButtons(buttons)) == juce::StringArray{"Don't Save", "Cancel", "Save"});
    REQUIRE(alert::defaultResult(buttons) == 1);
    REQUIRE(alert::cancelResult(buttons) == 0);
}

TEST_CASE("Alert: two buttons put the cancel first and the main action last", "[ui][alert]")
{
    const std::vector<alert::Button> buttons{{"Rename", 1, Role::Main}, {"Cancel", 0, Role::Cancel}};

    REQUIRE(textsOf(alert::orderedButtons(buttons)) == juce::StringArray{"Cancel", "Rename"});
    REQUIRE(alert::defaultResult(buttons) == 1);
    REQUIRE(alert::cancelResult(buttons) == 0);
}

TEST_CASE("Alert: a message's OK is both what Enter and Escape choose", "[ui][alert]")
{
    const auto options = alert::message("Default saved", "File > New now starts from this board.");

    REQUIRE(options.buttons.size() == 1);
    REQUIRE(options.buttons.front().text == "OK");
    REQUIRE(alert::defaultResult(options.buttons) == 1);
    REQUIRE(alert::cancelResult(options.buttons) == 1);
}

TEST_CASE("Alert: with no main action Enter does nothing, and with no cancel Escape answers 0", "[ui][alert]")
{
    const std::vector<alert::Button> buttons{{"Left", 3, Role::Other}, {"Right", 4, Role::Other}};

    REQUIRE_FALSE(alert::defaultResult(buttons).has_value());
    REQUIRE(alert::cancelResult(buttons) == 0);
    // Buttons of the same role keep the order they were given.
    REQUIRE(textsOf(alert::orderedButtons(buttons)) == juce::StringArray{"Left", "Right"});
}

TEST_CASE("Alert: a response reads back a field by name, and an unknown field is empty", "[ui][alert]")
{
    alert::Response response;
    response.result = 1;
    response.fields["name"] = "Verse Clean";

    REQUIRE(response.field("name") == "Verse Clean");
    REQUIRE(response.field("missing").isEmpty());
}
