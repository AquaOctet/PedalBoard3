#include "AboutWindow.h"

#include "ColourScheme.h"
#include "DialogFrame.h"
#include "Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

namespace
{

constexpr int kWidth = 460;
constexpr int kHeight = 190;
constexpr int kMarginX = 26;
constexpr int kMarginY = 22;

// The mockup's About (UI chrome U4): the name in the display face with a teal
// "3", the version in mono.
class AboutPanel : public juce::Component
{
public:
    AboutPanel()
    {
        closeButton_.onClick = [this]
        {
            if (auto* frame = findParentComponentOfClass<DialogFrame>())
                frame->closeButtonPressed();
        };
        addAndMakeVisible(closeButton_);
        setSize(kWidth, kHeight);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(tokens::bgWindow);
        auto area = getLocalBounds().reduced(kMarginX, kMarginY);

        const auto title = fonts::display(34.0f);
        juce::AttributedString name;
        name.append("Pedalboard ", title, tokens::fgPrimary);
        name.append("3", title, tokens::accentPrimary);
        name.draw(g, area.removeFromTop(40).toFloat());

        g.setFont(fonts::mono(12.0f));
        g.setColour(tokens::fgMuted);
        g.drawText(
            "Version " JUCE_APPLICATION_VERSION_STRING, area.removeFromTop(20), juce::Justification::centredLeft);

        area.removeFromTop(14);
        g.setFont(fonts::ui(14.0f));
        g.setColour(tokens::fgSecondaryText);
        g.drawText("Based on Pedalboard2 by Niall Moody. Built with JUCE.",
                   area.removeFromTop(20),
                   juce::Justification::centredLeft);
    }

    void resized() override { closeButton_.setBounds(getWidth() - kMarginX - 80, getHeight() - kMarginY - 30, 80, 30); }

private:
    juce::TextButton closeButton_{"Close"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};

} // namespace

std::unique_ptr<juce::Component> makeAboutPanel()
{
    return std::make_unique<AboutPanel>();
}

void showAboutWindow()
{
    DialogFrame::launchModal("About Pedalboard 3", makeAboutPanel(), false);
}

} // namespace pedalboard3::ui
