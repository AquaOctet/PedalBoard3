#pragma once

#include <juce_core/juce_core.h>

namespace pedalboard3::session
{

// What a newer version of the app wrote that this one doesn't know (M5 U4, plan
// KD7, AC-FS-7): one element's unknown attributes and child elements, kept when
// a board is read and written back when it's saved, so a round trip through
// this version doesn't lose them.
//
// A plain value: copied with the bank, patch or node that carries it, and
// compared with ==.
struct UnknownXml
{
    juce::StringPairArray attributes{false}; // name to value, in file order; names are case-sensitive
    juce::StringArray elements;              // each unknown child element, as one line of XML

    bool isEmpty() const noexcept { return attributes.size() == 0 && elements.isEmpty(); }
    bool operator==(const UnknownXml& other) const
    {
        return attributes == other.attributes && elements == other.elements;
    }

    // What `el` carries beyond the attributes named in `knownAttributes` and the
    // child elements tagged in `knownChildren`.
    static UnknownXml collect(const juce::XmlElement& el,
                              const juce::StringArray& knownAttributes,
                              const juce::StringArray& knownChildren);

    // Keep one child element whole (a mapping of a kind this version doesn't know).
    void keep(const juce::XmlElement& child);

    // Write them back onto `el`: each attribute it doesn't already have, then each
    // element as its last children.
    void writeTo(juce::XmlElement& el) const;
};

} // namespace pedalboard3::session
