#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// DeviceBadge — the audio device at a glance, at the left of the top bar
// (#90). The UI demo puts it there and the app had it in the status bar's
// right-hand corner, which is where you look last.
//
//   [ASIO 48kHz]  🎤 Scarlett 2i2 [1,2]   🔊 Scarlett 2i2 [1,2]
//
// A click opens Settings > Audio Devices, so the readout is also the way in to
// changing it. With no device the chip reads the driver type the device
// manager is set to and the names are replaced by the reason there is no
// sound, in amber; the tooltip carries the full error, which is usually too
// long for the bar.
//
// Three things make it read like the demo's rather than like a dump of what
// Windows calls things:
//   - the driver is shortened ("Windows Audio" is WASAPI, and nobody calls it
//     the other thing),
//   - the device name has its channel spec and Windows' duplicate-card
//     enumerator stripped, keeping whichever half actually names the
//     hardware,
//   - the active channel pair follows each name in the accent colour, which
//     is the part that changes when you re-patch.
// Both ends are shown even when one device serves both: with the pairs beside
// them the two lines say different things.
//
// Folding, cheapest loss first (#114):
//   0  chip + input + output, each with its channel pair
//   1  the channel pairs go -- both device names stay
//   2  the output device goes
//   3  the input goes too, leaving the driver chip
// The owner picks the level, preferredWidth() says what the current one
// needs, and widthAtFold() prices one without applying it.
//
// Threading: message thread only. Listens to the AudioDeviceManager.
class DeviceBadge : public juce::Button, private juce::ChangeListener
{
public:
    explicit DeviceBadge(AppContext& ctx);
    ~DeviceBadge() override;

    static constexpr int kMaxFoldLevel = 3;

    void setFoldLevel(int level);
    int preferredWidth() const noexcept { return preferredWidth_; }
    // What the badge would need at `level`, without changing it.
    int widthAtFold(int level) const;

    // The badge's content changed, so the owner should lay out again: a device
    // change can make it wider or narrower.
    std::function<void()> onLayoutChanged;

private:
    void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void measure();
    int deviceWidthAt(int level, bool input) const;

    AppContext& ctx_;

    juce::String driver_;      // "ASIO", "WASAPI", "CoreAudio"...
    juce::String rate_;        // "48kHz"
    juce::String inName_;      // input device, shortened; empty when there is none
    juce::String outName_;     // output device, shortened
    juce::String inChannels_;  // "[1,2]", empty when no channel is active
    juce::String outChannels_; // "[1,2]"
    juce::String problem_;     // why there is no device, empty when there is one

    int foldLevel_{0};
    int preferredWidth_{0};
    int chipWidth_{0};
    int inWidth_{0};
    int outWidth_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceBadge)
};

} // namespace pedalboard3::ui
