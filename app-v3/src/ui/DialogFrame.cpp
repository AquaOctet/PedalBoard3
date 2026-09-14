#include "DialogFrame.h"

#include "ColourScheme.h"
#include "Pb3LookAndFeel.h"

namespace pedalboard3::ui
{

DialogFrame::DialogFrame(const juce::String& title, bool addToDesktop)
    : juce::DocumentWindow(title, tokens::bgWindow, juce::DocumentWindow::closeButton, addToDesktop),
      lookAndFeel_(std::make_unique<Pb3DialogLookAndFeel>())
{
    // Installing it rebuilds the title-bar buttons (DocumentWindow::lookAndFeelChanged).
    setLookAndFeel(lookAndFeel_.get());
    setUsingNativeTitleBar(false);
    setTitleBarHeight(dialogframe::kTitleBarHeight);
    setTitleBarButtonsRequired(juce::DocumentWindow::closeButton, false);
    setTitleBarTextCentred(false);
}

DialogFrame::~DialogFrame()
{
    // The content and the title-bar buttons use the frame's LookAndFeel, so
    // they go before it does.
    clearContentComponent();
    setLookAndFeel(nullptr);
}

void DialogFrame::launchModal(const juce::String& title, std::unique_ptr<juce::Component> content, bool resizable)
{
    auto* frame = new DialogFrame(title);
    // Resizable first: it widens the border the content sits inside.
    frame->setResizable(resizable, false);
    if (resizable)
        frame->setResizeLimits(360, 200, 8192, 8192);
    frame->setContentOwned(content.release(), true);
    frame->centreWithSize(frame->getWidth(), frame->getHeight());
    frame->setVisible(true);
    frame->enterModalState(true, nullptr, true); // deleted when dismissed
}

void DialogFrame::closeButtonPressed()
{
    if (isCurrentlyModal(false))
        exitModalState(0);
    else
        setVisible(false);
}

void DialogFrame::childrenChanged()
{
    juce::DocumentWindow::childrenChanged();
    // JUCE sends a LookAndFeel change on setLookAndFeel, not when a component
    // gets a new parent (juce_Component.cpp).
    if (auto* content = getContentComponent(); content != nullptr && content != lastContent_.getComponent())
    {
        lastContent_ = content;
        content->sendLookAndFeelChange();
    }
}

bool DialogFrame::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeButtonPressed();
        return true;
    }
    return juce::DocumentWindow::keyPressed(key);
}

} // namespace pedalboard3::ui
