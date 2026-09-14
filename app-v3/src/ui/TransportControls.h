#pragma once

#include "AppContext.h"
#include "ColourScheme.h"
#include "session/NodeId.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pedalboard3::engine::processors
{
class MetronomeProcessor;
}

namespace pedalboard3::ui
{

// The top bar's transport pill (M6 U4, redone in optional-features U4, KD8):
//
//   ( ▶  ⏮ │ 120.0 BPM ▾ │ 3.2 │ ♩ )
//
//   - Play / Pause and Return to Zero
//   - the patch's tempo: a click opens the tempo popover (-/+, drag or type,
//     Tap, and the time signature)
//   - the position, as bar.beat
//   - ♩: the patch's Metronome on and off. It shows while the Metronome is
//     available, the patch has one, and Settings > Metronome shows it.
// TransportBar shows the pill only while Tempo / BPM is available. Folded (a
// narrow window), the position goes first, then ♩. It polls the transport at
// 30 Hz, like the CPU meter.
//
// Message thread only. Listens to Session (the patch's Metronome) and to the
// features.
class TransportControls : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit TransportControls(AppContext& ctx);
    ~TransportControls() override;

    // 0: everything; 1: without the position; 2: without the position and ♩.
    void setFoldLevel(int level);
    int preferredWidth() const noexcept;

    // ♩ came or went, so the bar should lay out again.
    std::function<void()> onLayoutChanged;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override; // the patch or the features changed
    void refresh();
    void openTempoPopover();
    void promptTimeSignature();
    void toggleMetronome();
    bool clickShown() const noexcept { return wantClick_ && foldLevel_ < 2; }
    engine::processors::MetronomeProcessor* metronome() const;

    AppContext& ctx_;
    int foldLevel_{0};
    bool shownPlaying_{false};
    bool wantClick_{false};
    std::optional<session::NodeId> metronomeNode_; // the patch's first Metronome
    std::vector<int> dividers_;                    // where the group dividers go

    juce::ShapeButton playButton_{"Play", tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary};
    juce::ShapeButton returnButton_{"Return to Zero", tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary};
    juce::TextButton tempoButton_;
    juce::Label position_;
    juce::TextButton clickButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportControls)
};

} // namespace pedalboard3::ui
