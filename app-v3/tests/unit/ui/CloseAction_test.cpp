// Tests for ui::closeActionFor (ui/CloseAction.h): what the main window's
// close button does (#147). The window itself needs a desktop; this decision
// does not, and it is the part that can strand a user.

#include <catch2/catch_test_macros.hpp>

#include "ui/CloseAction.h"

using namespace pedalboard3::ui;

TEST_CASE("closeActionFor: on by default, closing keeps the app running", "[ui][tray]")
{
    REQUIRE(closeActionFor(/*settingEnabled*/ true, /*trayAvailable*/ true) == CloseAction::HideToTray);
}

TEST_CASE("closeActionFor: with the setting off, closing quits", "[ui][tray]")
{
    REQUIRE(closeActionFor(false, true) == CloseAction::Quit);
}

TEST_CASE("closeActionFor: with no tray, closing quits however the setting is set", "[ui][tray]")
{
    // The one that matters. A desktop with no appindicator host gives JUCE
    // nowhere to put the icon, and a window that hides with nothing to restore
    // it is an app that has vanished while still holding the audio device.
    // Quitting is the right answer even though the user asked for the other
    // one, because the other one is not available.
    REQUIRE(closeActionFor(true, false) == CloseAction::Quit);
    REQUIRE(closeActionFor(false, false) == CloseAction::Quit);
}
