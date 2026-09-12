#pragma once

#include "AppContext.h"
#include "control/MidiMapping.h"
#include "control/OscMapping.h"
#include "MappingRowEditors.h"
#include "session/NodeId.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace pedalboard3::ui
{

// Per-plugin MIDI / OSC mappings editor (roadmap M4 U4, plan
// docs/plans/2026-09-11-002-feat-mappings-ui-plan.md, KD1 / KD6 / KD7).
//
// Lists the node's MIDI mappings (source kind, channel, number, target
// parameter, range min/max, invert, latch, learn, delete) and OSC mappings
// (address, argument index, parameter, range, invert, delete), and sets the
// node's MIDI channel filter. The row editors are shared with the board
// mappings panel (ui/MappingRowEditors.h).
//
// Edits are live: every change hands the node's whole mapping tables to
// Session::setNodeMappings (one call per edit, KD6), so ControlWiring re-feeds
// the hub at once and the next save persists them. The channel filter goes
// through Session::setNodeMidiChannel. The panel follows Session changes, so an
// edit made in the board mappings panel (UI refresh U9) shows here too.
//
// Learn arms ControlHub for one MIDI row; the next learnable message becomes
// that row's source. Destroying the panel cancels an armed learn.
class MappingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    MappingsPanel(AppContext& ctx, session::NodeId node);
    ~MappingsPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void rebuildRows();
    void layoutRows();
    void apply();
    void startLearn(int midiIndex);
    void cancelLearn();
    void deleteMidiLater(int index);
    void deleteOscLater(int index);

    AppContext& ctx_;
    session::NodeId node_;
    juce::StringArray parameterNames_;
    std::vector<control::MidiMapping> midi_;
    std::vector<control::OscMapping> osc_;
    int learningRow_{-1};

    juce::Label hintLabel_;
    juce::Label channelLabel_;
    juce::ComboBox channelBox_;
    juce::Label footerLabel_;

    juce::Viewport viewport_;
    juce::Component rowsContent_;
    juce::Label midiHeader_;
    juce::Label oscHeader_;
    juce::TextButton addMidiButton_{"+ MIDI mapping"};
    juce::TextButton addOscButton_{"+ OSC mapping"};
    std::vector<std::unique_ptr<MidiMappingRow>> midiRows_;
    std::vector<std::unique_ptr<OscMappingRow>> oscRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MappingsPanel)
};

// The window around a MappingsPanel. SignalCanvas owns at most one and
// destroys it before anything that could invalidate its node: the node's
// removal, a whole-patch swap (node ids can be reused), canvas teardown.
// The close button calls `onClose`, which the owner uses to drop its handle
// (the same contract as EditorWindow).
class MappingsWindow : public juce::DocumentWindow
{
public:
    MappingsWindow(AppContext& ctx, session::NodeId node, const juce::String& nodeName, std::function<void()> onClose);

    void closeButtonPressed() override;

    session::NodeId getNodeId() const noexcept { return node_; }

private:
    std::function<void()> onClose_;
    session::NodeId node_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MappingsWindow)
};

} // namespace pedalboard3::ui
