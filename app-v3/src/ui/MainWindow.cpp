#include "MainWindow.h"

namespace pedalboard3::ui
{

namespace
{
class HelloWorldComponent : public juce::Component
{
public:
    HelloWorldComponent() { setSize(640, 360); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(32.0f, juce::Font::plain));
        g.drawFittedText("Pedalboard 3", getLocalBounds().removeFromTop(getHeight() / 2),
                         juce::Justification::centred, 1);
        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(14.0f, juce::Font::plain));
        g.drawFittedText("Phase 0 scaffold — ready for Phase 1 engine work.",
                         getLocalBounds().removeFromBottom(getHeight() / 2),
                         juce::Justification::centred, 1);
    }
};
} // namespace

MainWindow::MainWindow(const juce::String& name)
    : juce::DocumentWindow(name,
                           juce::Desktop::getInstance()
                               .getDefaultLookAndFeel()
                               .findColour(juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new HelloWorldComponent(), true);
    setResizable(true, true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace pedalboard3::ui
