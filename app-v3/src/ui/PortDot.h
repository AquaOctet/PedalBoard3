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

// Where the ports sit, shared by the plugin cards and the canvas's Input and
// Output endpoints (#123).
//
// They were separate before: a card put L/R/MIDI at fixed offsets from its
// top, while an endpoint spread them over quarters of its height. Nothing
// lined up, so every cable ran at a slight slope and three of them together
// read as a tangle rather than as a bundle. One set of offsets, and a cable
// between two things at the same height is a straight line.
//
// L and R are close together because they are one stereo pair and should
// look like one; MIDI sits well below them because it is a different kind of
// signal, and the gap is the only thing on the canvas that says so.
inline constexpr int kPortYL = 44;
inline constexpr int kPortYR = 56;    // 12 below L: a pair
inline constexpr int kPortYMidi = 78; // 22 below R: something else
inline constexpr int kInX = 12;       // from the left edge
inline constexpr int kOutInset = 10;  // from the right edge

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
// A dot takes the colour of the cable END it belongs to, so a dot and the
// cable leaving it are the same colour and the eye follows one into the
// other: purple out of a source, green into a destination, and the same for
// MIDI's yellow and orange.
inline juce::Colour audioIn()
{
    return tokens::cableAudioTo;
}
inline juce::Colour audioOut()
{
    return tokens::cableAudioFrom;
}
inline juce::Colour midiIn()
{
    return tokens::cableMidiTo;
}
inline juce::Colour midiOut()
{
    return tokens::cableMidiFrom;
}

} // namespace pedalboard3::ui::ports
