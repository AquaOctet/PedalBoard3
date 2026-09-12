#pragma once

#include "AppContext.h"
#include "ColourScheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace pedalboard3::ui
{

// The top bar's transport (M6 U4, plan KD1 / KD3):
//   - Play / Pause and Return to Zero
//   - the tempo: drag it up or down (Shift for fine steps) or double-click to
//     type, with a Tap button beside it
//   - the time signature, from a menu of common ones or typed
//   - the position, as bar.beat
// The tempo and time signature belong to the playing patch, so edits go
// through Session and are saved with it; the buttons run the same transport
// commands as the keys and mappings. It polls the transport at 30 Hz, like
// the CPU meter. Folded (a narrow window), the time signature and position
// are hidden.
//
// Message thread only.
class TransportControls : public juce::Component, private juce::Timer
{
public:
    static constexpr int kFullWidth = 322;
    static constexpr int kFoldedWidth = 190;

    explicit TransportControls(AppContext& ctx);
    ~TransportControls() override;

    void setFolded(bool folded);
    void resized() override;

private:
    class TempoField;

    void timerCallback() override;
    void refresh();
    void showTimeSignatureMenu();
    void promptTimeSignature();

    AppContext& ctx_;
    bool folded_{false};
    bool shownPlaying_{false};

    juce::ShapeButton playButton_{"Play", tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary};
    juce::ShapeButton returnButton_{"Return to Zero", tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary};
    std::unique_ptr<TempoField> tempo_;
    juce::TextButton tapButton_{"Tap"};
    juce::TextButton timeSignatureButton_{"4/4"};
    juce::Label position_;
    std::unique_ptr<juce::AlertWindow> prompt_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportControls)
};

} // namespace pedalboard3::ui
