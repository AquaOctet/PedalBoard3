#include "Alert.h"

#include "ColourScheme.h"
#include "DialogFrame.h"
#include "Fonts.h"
#include "Pb3LookAndFeel.h"

#include <cmath>

namespace pedalboard3::ui::alert
{

namespace
{

constexpr int kWidth = 420;
constexpr int kPad = 14;
constexpr int kGap = 10;
constexpr int kFieldHeight = 28;
constexpr int kButtonHeight = 34;
constexpr int kButtonGap = 8;
constexpr int kButtonMinWidth = 88;
constexpr float kMessageSize = 14.0f;
constexpr float kButtonTextSize = 14.0f;
constexpr float kFieldTextSize = 13.0f;

// The message: 14 px fgSecondaryText, a name in quotes in fgPrimary SemiBold.
juce::AttributedString messageText(const juce::String& message)
{
    juce::AttributedString text;
    text.setWordWrap(juce::AttributedString::byWord);
    text.setJustification(juce::Justification::topLeft);
    text.setLineSpacing(2.0f);
    const auto plain = fonts::ui(kMessageSize);
    const auto name = fonts::ui(kMessageSize, fonts::Weight::SemiBold);

    int pos = 0;
    while (pos < message.length())
    {
        const int open = message.indexOfChar(pos, '"');
        const int close = open >= 0 ? message.indexOfChar(open + 1, '"') : -1;
        if (open < 0 || close < 0)
        {
            text.append(message.substring(pos), plain, tokens::fgSecondaryText);
            break;
        }
        if (open > pos)
            text.append(message.substring(pos, open), plain, tokens::fgSecondaryText);
        text.append(message.substring(open, close + 1), name, tokens::fgPrimary);
        pos = close + 1;
    }
    return text;
}

class AlertPanel : public juce::Component
{
public:
    AlertPanel(Options options, std::function<void(const Response&)> onResult)
        : options_(std::move(options)), onResult_(std::move(onResult)), text_(messageText(options_.message))
    {
        setWantsKeyboardFocus(true);

        for (const auto& field : options_.fields)
        {
            auto editor = std::make_unique<juce::TextEditor>(field.name);
            editor->setText(field.text, false);
            editor->setFont(fonts::ui(kFieldTextSize));
            editor->setIndents(10, 7);
            editor->onReturnKey = [this] { chooseDefault(); };
            editor->onEscapeKey = [this] { choose(cancelResult(options_.buttons)); };
            addAndMakeVisible(*editor);
            editors_.push_back(std::move(editor));
        }

        for (const auto& spec : orderedButtons(options_.buttons))
        {
            auto button = std::make_unique<juce::TextButton>(spec.text);
            if (spec.role != Role::Main)
                button->getProperties().set(Pb3LookAndFeel::kOutlineButton, true);
            if (spec.destructive)
                button->setColour(juce::TextButton::textColourOffId, tokens::accentDestructive);
            button->onClick = [this, result = spec.result] { choose(result); };
            addAndMakeVisible(*button);
            buttons_.push_back(std::move(button));
        }

        const int messageWidth = kWidth - 2 * kPad;
        juce::TextLayout layout;
        layout.createLayout(text_, static_cast<float>(messageWidth));
        messageHeight_ = static_cast<int>(std::ceil(layout.getHeight()));

        const int fieldCount = static_cast<int>(editors_.size());
        const int fieldsHeight = fieldCount > 0 ? kGap + fieldCount * kFieldHeight + (fieldCount - 1) * kGap : 0;
        setSize(kWidth, kPad + messageHeight_ + fieldsHeight + 2 * kGap + kButtonHeight + kPad);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(tokens::bgWindow);
        text_.draw(g, messageArea_.toFloat());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kPad);
        messageArea_ = area.removeFromTop(messageHeight_);
        for (auto& editor : editors_)
        {
            area.removeFromTop(kGap);
            editor->setBounds(area.removeFromTop(kFieldHeight));
        }

        auto row = area.removeFromBottom(kButtonHeight);
        for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it)
        {
            const int width =
                juce::jmax(kButtonMinWidth,
                           juce::GlyphArrangement::getStringWidthInt(fonts::ui(kButtonTextSize, fonts::Weight::Medium),
                                                                     (*it)->getButtonText()) +
                               32);
            (*it)->setBounds(row.removeFromRight(width));
            row.removeFromRight(kButtonGap);
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            chooseDefault();
            return true;
        }
        return false; // Escape reaches the frame, which cancels
    }

    // The first field, or the panel, takes the keyboard.
    void focusFirst()
    {
        if (!editors_.empty())
        {
            editors_.front()->grabKeyboardFocus();
            editors_.front()->selectAll();
        }
        else
        {
            grabKeyboardFocus();
        }
    }

    void cancel() { choose(cancelResult(options_.buttons)); }

private:
    void chooseDefault()
    {
        if (const auto result = defaultResult(options_.buttons))
            choose(*result);
    }

    void choose(int result)
    {
        if (done_)
            return;
        done_ = true;

        Response response;
        response.result = result;
        for (const auto& editor : editors_)
            response.fields[editor->getName()] = editor->getText();

        // Close first, then answer: the answer may open another dialog. The
        // frame deletes itself later, so this panel is still here meanwhile.
        auto callback = onResult_;
        if (auto* frame = findParentComponentOfClass<DialogFrame>())
            if (frame->isCurrentlyModal(false))
                frame->exitModalState(result);
        if (callback)
            callback(response);
    }

    Options options_;
    std::function<void(const Response&)> onResult_;
    juce::AttributedString text_;
    int messageHeight_{0};
    juce::Rectangle<int> messageArea_;
    std::vector<std::unique_ptr<juce::TextEditor>> editors_;
    std::vector<std::unique_ptr<juce::TextButton>> buttons_;
    bool done_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlertPanel)
};

// The frame's close cross and Escape cancel the alert.
class AlertFrame : public DialogFrame
{
public:
    AlertFrame(Options options, std::function<void(const Response&)> onResult) : DialogFrame(options.title)
    {
        auto panel = std::make_unique<AlertPanel>(std::move(options), std::move(onResult));
        panel_ = panel.get();
        setContentOwned(panel.release(), true);
    }

    void closeButtonPressed() override { panel_->cancel(); }

    AlertPanel& panel() noexcept { return *panel_; }

private:
    AlertPanel* panel_{nullptr};
};

} // namespace

juce::Component::SafePointer<juce::Component> show(Options options, std::function<void(const Response&)> onResult)
{
    auto* frame = new AlertFrame(std::move(options), std::move(onResult));
    frame->centreWithSize(frame->getWidth(), frame->getHeight());
    frame->setVisible(true);
    frame->enterModalState(true, nullptr, true); // deleted when dismissed
    frame->panel().focusFirst();
    return frame;
}

std::unique_ptr<juce::Component> makePanel(Options options, std::function<void(const Response&)> onResult)
{
    return std::make_unique<AlertPanel>(std::move(options), std::move(onResult));
}

} // namespace pedalboard3::ui::alert
