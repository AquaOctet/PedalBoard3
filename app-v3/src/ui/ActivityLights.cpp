#include "ActivityLights.h"

#include "ColourScheme.h"
#include "session/ControlHub.h"

namespace pedalboard3::ui
{

namespace
{

constexpr float kDotSize = 9.0f;

void drawLight(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, bool lit, juce::Colour colour)
{
    const auto r = area.toFloat();
    const juce::Rectangle<float> dot(r.getX() + 3.0f, r.getCentreY() - kDotSize / 2.0f, kDotSize, kDotSize);

    if (lit)
    {
        g.setColour(colour.withAlpha(0.35f)); // glow
        g.fillEllipse(dot.expanded(3.0f));
    }
    g.setColour(lit ? colour : tokens::bgSecondary);
    g.fillEllipse(dot);

    g.setColour(tokens::fgMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(label, r.withTrimmedLeft(kDotSize + 9.0f), juce::Justification::centredLeft);
}

} // namespace

ActivityLights::ActivityLights(session::ControlHub& hub) : hub_(hub)
{
    setTooltip("MIDI and OSC input: each light blinks when a message arrives, mapped or not");
    startTimerHz(30);
}

ActivityLights::~ActivityLights()
{
    stopTimer();
}

void ActivityLights::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const bool midi = midiBlink_.update(hub_.midiActivity(), now);
    const bool osc = oscBlink_.update(hub_.oscActivity(), now);
    if (midi != midiLit_ || osc != oscLit_)
    {
        midiLit_ = midi;
        oscLit_ = osc;
        repaint();
    }
}

void ActivityLights::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    drawLight(g, area.removeFromLeft(area.getWidth() / 2), "MIDI", midiLit_, tokens::accentInfo);
    drawLight(g, area, "OSC", oscLit_, tokens::accentPrimary);
}

} // namespace pedalboard3::ui
