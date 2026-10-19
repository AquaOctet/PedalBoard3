#pragma once

#include "AppContext.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace pedalboard3::ui
{

// PatchStrip — which patch is playing, and the two steps either side of it
// (#109). One control:
//
//   ┌──────┬──────────────────────────┬──────┐
//   │  ‹   │  2/6  Verse Clean     ▾  │  ›   │
//   └──────┴──────────────────────────┴──────┘
//
// It replaces three detached bordered boxes — `[<]`, a wide slab, `[>]` — that
// read as three unrelated controls and gave no sign that the middle one opens
// the bank menu. The caret says so; the dividers say the three parts belong
// together.
//
// The whole thing is painted by the strip rather than by its children. The
// children are transparent hit targets, and `paint` asks each one whether the
// mouse is over it. That is what lets a hover wash on the first or last
// segment be clipped to the container's rounded corners instead of sticking
// out of them.
//
// Width is content-driven: the name takes what it needs up to a cap, so a
// short patch name no longer leaves a slab of empty border. The bar asks for
// `preferredWidth()` and may give less, in which case the name ellipsises —
// it is still the first thing on the bar to give way.
//
// Threading: message thread only. Listens to BankDocument.
class PatchStrip : public juce::Component, private juce::ChangeListener
{
public:
    explicit PatchStrip(AppContext& ctx);
    ~PatchStrip() override;

    // The name was clicked: the owner opens the bank menu, since it owns the
    // menu's actions.
    std::function<void()> onOpenMenu;

    // The narrowest the strip ever gets: both chevrons plus the floor the
    // name block is clamped to. The bar prices its own minimum window size
    // against this rather than against preferredWidth(), which follows the
    // patch NAME and would otherwise make the window's minimum jump about
    // every time a patch was renamed (#116).
    static constexpr int kMinimumWidth = 176;

    // The width the current patch name wants, between that floor and a cap.
    int preferredWidth() const noexcept { return preferredWidth_; }

    // The strip changed width, so the owner should lay out again.
    std::function<void()> onLayoutChanged;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Segment; // a transparent hit target; the strip paints it

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();

    AppContext& ctx_;

    juce::String index_; // "2"
    juce::String count_; // "/6"
    juce::String name_;  // "Verse Clean"
    int preferredWidth_{0};

    std::unique_ptr<Segment> prev_;
    std::unique_ptr<Segment> name_button_;
    std::unique_ptr<Segment> next_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchStrip)
};

} // namespace pedalboard3::ui
