#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

// DeviceBadge — the audio device at a glance, at the left of the top bar
// (#90). The UI demo puts it there and the app had it in the status bar's
// right-hand corner, which is where you look last.
//
//   [ASIO 48kHz]  🎤 Scarlett 2i2   🔊 Scarlett 2i2
//
// A click opens Settings > Audio Devices, so the readout is also the way in to
// changing it. With no device the chip reads the driver type the device
// manager is set to and the names are replaced by the reason there is no
// sound, in amber; the tooltip carries the full error, which is usually too
// long for the bar.
//
// Folding mirrors the demo's own breakpoints (`hidden sm:flex` on the input,
// `hidden md:flex` on the output): level 0 shows all three parts, 1 drops the
// output device, 2 drops the input too and leaves the driver chip. The owner
// picks the level from the window width, and preferredWidth() says how much
// room the current level needs.
//
// Threading: message thread only. Listens to the AudioDeviceManager.
class DeviceBadge : public juce::Button, private juce::ChangeListener
{
public:
    explicit DeviceBadge(AppContext& ctx);
    ~DeviceBadge() override;

    // 0 = chip + input + output, 1 = chip + input, 2 = chip only.
    void setFoldLevel(int level);
    int preferredWidth() const noexcept { return preferredWidth_; }

    // The badge's content changed, so the owner should lay out again: a device
    // change can make it wider or narrower.
    std::function<void()> onLayoutChanged;

private:
    void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void measure();

    AppContext& ctx_;

    juce::String driver_;  // "ASIO", "WASAPI", "CoreAudio"...
    juce::String rate_;    // "48kHz"
    juce::String inName_;  // input device, empty when there is none
    juce::String outName_; // output device, empty when there is none
    juce::String problem_; // why there is no device, empty when there is one

    int foldLevel_{0};
    int preferredWidth_{0};
    int chipWidth_{0};
    int inWidth_{0};
    int outWidth_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceBadge)
};

} // namespace pedalboard3::ui
