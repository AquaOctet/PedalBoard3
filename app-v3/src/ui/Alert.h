#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pedalboard3::ui::alert
{

// One alert for every message, confirmation and prompt (UI chrome plan
// docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U5 / KD4), in the dialog
// frame: the title in the title bar, the message in 14 px with names in
// quotes picked out, optional text fields, and a right-aligned button row
// with the main action last. No icon. Enter runs the main action; Escape and
// the close cross cancel.

// What a button is for.
enum class Role
{
    Main,   // last, filled, and what Enter runs
    Cancel, // what Escape and the close cross choose
    Other,  // anything else ("Don't Save"), first
};

struct Button
{
    juce::String text;
    int result{0};
    Role role{Role::Other};
    bool destructive{false}; // red text ("Remove")
};

struct Field
{
    juce::String name;
    juce::String text; // what it starts with
};

struct Options
{
    juce::String title;
    juce::String message;
    std::vector<Button> buttons;
    std::vector<Field> fields;
};

struct Response
{
    int result{0};
    std::map<juce::String, juce::String> fields; // name -> what was typed

    juce::String field(const juce::String& name) const
    {
        const auto it = fields.find(name);
        return it != fields.end() ? it->second : juce::String();
    }
};

// The buttons left to right: the others, then the cancel, then the main
// action (KD4: Don't Save, Cancel, Save).
inline std::vector<Button> orderedButtons(std::vector<Button> buttons)
{
    const auto rank = [](Role role) { return role == Role::Other ? 0 : (role == Role::Cancel ? 1 : 2); };
    std::stable_sort(buttons.begin(),
                     buttons.end(),
                     [&rank](const Button& a, const Button& b) { return rank(a.role) < rank(b.role); });
    return buttons;
}

// What Enter chooses: the main action, or the only button. Nothing when
// neither.
inline std::optional<int> defaultResult(const std::vector<Button>& buttons)
{
    for (const auto& b : buttons)
        if (b.role == Role::Main)
            return b.result;
    if (buttons.size() == 1)
        return buttons.front().result;
    return std::nullopt;
}

// What Escape and the close cross choose: the cancel, or the only button (a
// message's OK), otherwise 0.
inline int cancelResult(const std::vector<Button>& buttons)
{
    for (const auto& b : buttons)
        if (b.role == Role::Cancel)
            return b.result;
    if (buttons.size() == 1)
        return buttons.front().result;
    return 0;
}

// A message with one OK.
inline Options message(const juce::String& title, const juce::String& text)
{
    return {title, text, {{"OK", 1, Role::Main}}, {}};
}

// Opens the alert in a modal dialog frame. `onResult` runs as it closes.
// Returns the frame, so a caller can bring a question it's already asking to
// the front.
juce::Component::SafePointer<juce::Component> show(Options options, std::function<void(const Response&)> onResult = {});

// The alert's content on its own, without its frame (the UI snapshots tool).
std::unique_ptr<juce::Component> makePanel(Options options, std::function<void(const Response&)> onResult = {});

} // namespace pedalboard3::ui::alert
