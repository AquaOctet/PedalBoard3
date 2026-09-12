#pragma once

#include "control/MidiMapping.h"
#include "control/OscMapping.h"
#include "session/NodeId.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace pedalboard3::session
{
class Session;
}

namespace pedalboard3::ui
{

// The MIDI and OSC mapping row editors, shared by the per-plugin mappings
// dialog (M4 U4) and the board mappings panel (UI refresh U9). A row edits one
// mapping and reports each change through onChange; its owner writes the
// node's mappings back with Session::setNodeMappings.

namespace mappingcols
{
inline constexpr int kGap = 4;
// MIDI: [kind][channel][number] [parameter ...] [min][max] [Inv][Latch] [Learn][X]
inline constexpr int kKind = 96;
inline constexpr int kChannel = 70;
inline constexpr int kNumber = 56;
// OSC: [address][argument] [parameter ...] [min][max] [Inv] [X]
inline constexpr int kAddress = 210;
inline constexpr int kArgument = 56;
inline constexpr int kMin = 64;
inline constexpr int kMax = 64;
inline constexpr int kInvert = 52;
inline constexpr int kLatch = 62;
inline constexpr int kLearn = 58;
inline constexpr int kDelete = 28;

// Where a row's column groups sit, so a header can label them.
struct Spans
{
    juce::Range<int> source;
    juce::Range<int> control;
    juce::Range<int> range;
    juce::Range<int> flags; // Inv and Latch (MIDI), or Inv (OSC)
};
Spans midiSpans(int rowWidth);
Spans oscSpans(int rowWidth);
} // namespace mappingcols

// A new mapping on `node`, as the "+" buttons add it: CC 1 on any channel, or
// an OSC address numbered `ordinal`, both driving parameter 0.
control::MidiMapping defaultMidiMapping(session::NodeId node);
control::OscMapping defaultOscMapping(session::NodeId node, int ordinal);

// The node's parameter names, for the parameter boxes; empty when it has no
// live processor. Reads the hosted processor, so call it while Session is idle.
juce::StringArray parameterNamesOf(session::Session&, session::NodeId);

class MidiMappingRow : public juce::Component
{
public:
    MidiMappingRow(const control::MidiMapping& mapping, const juce::StringArray& parameterNames);

    std::function<void(const control::MidiMapping&)> onChange;
    std::function<void()> onLearn;
    std::function<void()> onDelete;

    void setLearning(bool learning);
    // The board panel's Learn opens the plugin's dialog rather than arming here.
    void setLearnLabel(const juce::String& text, const juce::String& tooltip);

    void resized() override;

private:
    void pushChange();
    void updateNumberEnablement(); // Program Change and Pitch Bend have no number

    control::MidiMapping mapping_;
    juce::String learnText_{"Learn"};
    juce::ComboBox kind_;
    juce::ComboBox channel_;
    juce::Slider number_;
    juce::ComboBox param_;
    juce::Slider min_;
    juce::Slider max_;
    juce::ToggleButton invert_{"Inv"};
    juce::ToggleButton latch_{"Latch"};
    juce::TextButton learn_{"Learn"};
    juce::TextButton delete_{"X"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMappingRow)
};

class OscMappingRow : public juce::Component
{
public:
    OscMappingRow(const control::OscMapping& mapping, const juce::StringArray& parameterNames);

    std::function<void(const control::OscMapping&)> onChange;
    std::function<void()> onDelete;

    void resized() override;

private:
    void pushChange();

    control::OscMapping mapping_;
    juce::TextEditor address_;
    juce::Slider argument_;
    juce::ComboBox param_;
    juce::Slider min_;
    juce::Slider max_;
    juce::ToggleButton invert_{"Inv"};
    juce::TextButton delete_{"X"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscMappingRow)
};

} // namespace pedalboard3::ui
