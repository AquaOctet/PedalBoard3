#pragma once

#include "ColourScheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui::ports
{

// The little circle a cable attaches to.
//
// One definition, because there are two places that draw them and they have
// to be the same circle: the plugin cards, and the Input and Output
// endpoints on the canvas. The endpoints had none at all until #121 — cables
// simply began and ended in mid-air at the edge of a rounded rectangle,
// which made the two ends of the chain look like a different kind of thing
// from everything between them, and gave no clue where a cable could be
// dropped.
//
// Ringed in the window background so the dot reads against a card, against
// the endpoint's own fill, and against a cable passing behind it.
inline constexpr int kRadius = 5;

inline void drawDot(juce::Graphics& g, juce::Point<int> centre, juce::Colour colour)
{
    const auto box = juce::Rectangle<float>(0.0f, 0.0f, kRadius * 2.0f, kRadius * 2.0f).withCentre(centre.toFloat());
    g.setColour(colour);
    g.fillEllipse(box);
    g.setColour(tokens::bgWindow);
    g.drawEllipse(box, 1.5f);
}

// What a port's colour says, used by both: teal takes signal in, amber sends
// it out, cyan is MIDI either way. The endpoints follow the same rule rather
// than inventing one, so Input's dots are amber (it is a source) and
// Output's are teal (it is a destination) — the same amber-to-teal run the
// eye already follows from card to card.
inline juce::Colour audioIn()
{
    return tokens::accentPrimary;
}
inline juce::Colour audioOut()
{
    return tokens::accentWarning;
}
inline juce::Colour midi()
{
    return tokens::accentInfo;
}

} // namespace pedalboard3::ui::ports
