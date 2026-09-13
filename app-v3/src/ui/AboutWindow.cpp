#include "AboutWindow.h"

#include "ColourScheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedalboard3::ui
{

namespace
{

constexpr int kWidth = 460;
constexpr int kHeight = 190;
constexpr int kMarginX = 26;
constexpr int kMarginY = 22;

class AboutPanel : public juce::Component
{
public:
    AboutPanel()
    {
        closeButton_.onClick = [this]
        {
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
                dialog->exitModalState(0);
        };
        addAndMakeVisible(closeButton_);
        setSize(kWidth, kHeight);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(tokens::bgPopover);
        auto area = getLocalBounds().reduced(kMarginX, kMarginY);

        const juce::Font title(juce::FontOptions(34.0f).withStyle("Bold"));
        juce::AttributedString name;
        name.append("Pedalboard ", title, tokens::fgPrimary);
        name.append("3", title, tokens::accentPrimary);
        name.draw(g, area.removeFromTop(40).toFloat());

        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.setColour(tokens::fgMuted);
        g.drawText(
            "Version " JUCE_APPLICATION_VERSION_STRING, area.removeFromTop(20), juce::Justification::centredLeft);

        area.removeFromTop(14);
        g.setColour(tokens::fgPrimary);
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
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(makeAboutPanel().release());
    options.dialogTitle = "About Pedalboard 3";
    options.dialogBackgroundColour = tokens::bgPopover;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace pedalboard3::ui
