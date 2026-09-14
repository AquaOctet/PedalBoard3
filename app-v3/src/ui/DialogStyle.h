#pragma once

#include "ColourScheme.h"
#include "DialogFrame.h"
#include "Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui::dialogstyle
{

// The labels and the launch the dialogs share, rather than a copy in each.

// A section caption, as the mockup's (UI chrome U4): uppercase Barlow
// Condensed, letter-spaced.
inline void caption(juce::Label& label, const juce::String& text)
{
    label.setText(text.toUpperCase(), juce::dontSendNotification);
    label.setFont(fonts::caption(12.0f));
    label.setColour(juce::Label::textColourId, tokens::fgMuted);
}

// A muted note in a section: what an option does, or why a list is empty.
inline void hint(juce::Label& label,
                 const juce::String& text,
                 juce::Justification justification = juce::Justification::centredLeft)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(12.0f)));
    label.setColour(juce::Label::textColourId, tokens::fgMuted);
    label.setJustificationType(justification);
}

// The grey label beside a control, in the built-in processors' windows.
inline void fieldLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, tokens::fgMuted);
}

// A section's heading in the mappings windows.
inline void sectionHeader(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    label.setColour(juce::Label::textColourId, tokens::fgPrimary);
}

// Opens `content` in a modal dialog that owns it and can't be resized.
inline void launch(juce::Component* content, const juce::String& title)
{
    DialogFrame::launchModal(title, std::unique_ptr<juce::Component>(content), false);
}

} // namespace pedalboard3::ui::dialogstyle
