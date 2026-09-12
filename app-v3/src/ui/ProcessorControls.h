#pragma once

#include "AppContext.h"
#include "session/NodeId.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

// The control row on a built-in processor's card (M6 U5, plan KD7): the
// buttons and readouts a performer needs without opening a window. The card
// grows by kHeight to hold it.
//   - A row polls its processor at 15 Hz and writes its parameters on the
//     message thread, so a MIDI mapping and a click stay in step, and an
//     edit marks the board unsaved like any plugin edit.
//   - It reads the processor only while Session is idle; a rebuild can free
//     it.
//
// makeProcessorControls picks the row for a node's internal type, or returns
// nullptr for a built-in without one (Level, VU Meter).
class ProcessorControls : public juce::Component, private juce::Timer
{
public:
    static constexpr int kHeight = 32;

    ProcessorControls(AppContext& ctx, session::NodeId node);
    ~ProcessorControls() override;

protected:
    // The node's processor, or nullptr while Session isn't idle or the node has gone.
    juce::AudioProcessor* processor() const;
    // Its parameter with this id, when it has one.
    juce::AudioProcessorParameterWithID* parameter(const juce::String& id) const;

    // Show the processor's current state. Called on the timer and after a click.
    virtual void refresh() = 0;

    AppContext& ctx_;
    const session::NodeId node_;

private:
    void timerCallback() override { refresh(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorControls)
};

std::unique_ptr<ProcessorControls> makeProcessorControls(AppContext& ctx,
                                                         const juce::String& internalType,
                                                         session::NodeId node);

} // namespace pedalboard3::ui
