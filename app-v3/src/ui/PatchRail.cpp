#include "PatchRail.h"

#include "Icons.h"
#include "Pb3LookAndFeel.h"
#include "session/BankDocument.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kButtonSize = 36;
constexpr int kGap = 8;

juce::String programLabel(int index)
{
    return "PC" + juce::String(index);
}
} // namespace

PatchRail::PatchRail(AppContext& ctx) : ctx_(ctx)
{
    setOpaque(true);

    panelButton_.setShape(panelToggleIcon(), false, true, false);
    panelButton_.setTooltip("Open the patch bank");
    panelButton_.onClick = [this]
    {
        if (onOpenPanel)
            onOpenPanel();
    };

    previous_.setTooltip("Previous patch");
    previous_.onClick = [this] { ctx_.bankDocument.previous(); };
    next_.setTooltip("Next patch");
    next_.onClick = [this] { ctx_.bankDocument.next(); };

    // The playing patch reads as "on" (an outline button, toggled) and opens
    // the bank rather than switching.
    current_.setTooltip("The playing patch: open the bank");
    current_.getProperties().set(Pb3LookAndFeel::kOutlineButton, true);
    current_.setColour(juce::TextButton::textColourOnId, tokens::accentPrimary);
    current_.setToggleState(true, juce::dontSendNotification);
    current_.onClick = [this]
    {
        if (onOpenPanel)
            onOpenPanel();
    };

    for (auto* c : std::initializer_list<juce::Component*>{&panelButton_, &previous_, &current_, &next_})
        addAndMakeVisible(c);

    ctx_.bankDocument.addChangeListener(this);
    refresh();
}

PatchRail::~PatchRail()
{
    ctx_.bankDocument.removeChangeListener(this);
}

void PatchRail::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void PatchRail::refresh()
{
    const auto& doc = ctx_.bankDocument;
    const int count = doc.size();
    const int active = doc.activeIndex();
    current_.setButtonText(programLabel(active));
    previous_.setButtonText(programLabel((active + count - 1) % count)); // the bank wraps around
    next_.setButtonText(programLabel((active + 1) % count));
    previous_.setVisible(count > 1);
    next_.setVisible(count > 1);
}

void PatchRail::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
}

void PatchRail::resized()
{
    auto area = getLocalBounds().reduced(6, 10);
    panelButton_.setBounds(area.removeFromTop(kButtonSize).withSizeKeepingCentre(18, 18));
    area.removeFromTop(kGap + 4);
    previous_.setBounds(area.removeFromTop(kButtonSize));
    area.removeFromTop(kGap);
    current_.setBounds(area.removeFromTop(kButtonSize));
    area.removeFromTop(kGap);
    next_.setBounds(area.removeFromTop(kButtonSize));
}

} // namespace pedalboard3::ui
