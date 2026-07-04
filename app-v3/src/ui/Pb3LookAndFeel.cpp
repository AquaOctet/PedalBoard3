#include "Pb3LookAndFeel.h"

#include "ColourScheme.h"

namespace pedalboard3::ui
{

namespace
{
constexpr float kButtonCornerRadius = 4.0f;
constexpr float kTickBoxCornerRadius = 3.0f;
} // namespace

Pb3LookAndFeel::Pb3LookAndFeel()
{
    // V4 ColourScheme drives most defaults: dark blue-gray surfaces,
    // teal highlights, near-white text. The 9 slots map to nearly every
    // ColourId JUCE consults when a per-class setColour isn't set.
    setColourScheme({
        tokens::bgWindow,      //  windowBackground
        tokens::bgSecondary,   //  widgetBackground
        tokens::bgPopover,     //  menuBackground
        tokens::borderDefault, //  outline
        tokens::fgPrimary,     //  defaultText
        tokens::bgCard,        //  defaultFill
        tokens::fgInverse,     //  highlightedText (on accent fill, want dark)
        tokens::accentPrimary, //  highlightedFill
        tokens::fgPrimary,     //  menuText
    });

    // Default sans-serif typeface — a clean system font so the app and JUCE's own
    // dialogs (the plugin-list / scan window) render with a known-good face
    // instead of JUCE's default pick, which rendered poorly on Windows. No
    // bundled binary; uses a font that ships with the OS. macOS / Linux keep
    // JUCE's fontconfig/CoreText default (they render fine already).
#if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName("Segoe UI");
#endif

    // ----- TextButton -------------------------------------------------------
    setColour(juce::TextButton::buttonColourId, tokens::bgCard);
    setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
    setColour(juce::TextButton::textColourOnId, tokens::fgInverse);
    setColour(juce::TextButton::textColourOffId, tokens::fgPrimary);

    // ----- ToggleButton -----------------------------------------------------
    setColour(juce::ToggleButton::textColourId, tokens::fgPrimary);
    setColour(juce::ToggleButton::tickColourId, tokens::accentPrimary);
    setColour(juce::ToggleButton::tickDisabledColourId, tokens::fgMuted);

    // ----- Label / TextEditor ----------------------------------------------
    setColour(juce::Label::textColourId, tokens::fgPrimary);
    setColour(juce::Label::backgroundColourId, juce::Colour(0)); // transparent
    setColour(juce::TextEditor::backgroundColourId, tokens::bgSecondary);
    setColour(juce::TextEditor::textColourId, tokens::fgPrimary);
    setColour(juce::TextEditor::highlightColourId, tokens::accentPrimary.withAlpha(0.35f));
    setColour(juce::TextEditor::outlineColourId, tokens::borderDefault);
    setColour(juce::TextEditor::focusedOutlineColourId, tokens::accentPrimary);

    // ----- ComboBox ---------------------------------------------------------
    setColour(juce::ComboBox::backgroundColourId, tokens::bgSecondary);
    setColour(juce::ComboBox::textColourId, tokens::fgPrimary);
    setColour(juce::ComboBox::outlineColourId, tokens::borderDefault);
    setColour(juce::ComboBox::buttonColourId, tokens::bgSecondary);
    setColour(juce::ComboBox::arrowColourId, tokens::fgMuted);
    setColour(juce::ComboBox::focusedOutlineColourId, tokens::accentPrimary);

    // ----- PopupMenu --------------------------------------------------------
    setColour(juce::PopupMenu::backgroundColourId, tokens::bgPopover);
    setColour(juce::PopupMenu::textColourId, tokens::fgPrimary);
    setColour(juce::PopupMenu::headerTextColourId, tokens::fgMuted);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, tokens::accentPrimary);
    setColour(juce::PopupMenu::highlightedTextColourId, tokens::fgInverse);

    // ----- Slider (Audio Settings has these) -------------------------------
    setColour(juce::Slider::backgroundColourId, tokens::bgMuted);
    setColour(juce::Slider::trackColourId, tokens::bgSecondary);
    setColour(juce::Slider::thumbColourId, tokens::accentPrimary);
    setColour(juce::Slider::rotarySliderFillColourId, tokens::accentPrimary);
    setColour(juce::Slider::rotarySliderOutlineColourId, tokens::borderDefault);
    setColour(juce::Slider::textBoxTextColourId, tokens::fgPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, tokens::bgSecondary);
    setColour(juce::Slider::textBoxOutlineColourId, tokens::borderDefault);

    // ----- ScrollBar (Viewport scrollbars on long chains) ------------------
    setColour(juce::ScrollBar::backgroundColourId, tokens::bgWindow);
    setColour(juce::ScrollBar::thumbColourId, tokens::borderDefault);
    setColour(juce::ScrollBar::trackColourId, tokens::bgSecondary);

    // ----- TableHeader (PluginListComponent) -------------------------------
    setColour(juce::TableHeaderComponent::backgroundColourId, tokens::bgSecondary);
    setColour(juce::TableHeaderComponent::outlineColourId, tokens::borderDefault);
    setColour(juce::TableHeaderComponent::highlightColourId, tokens::accentPrimary.withAlpha(0.20f));
    setColour(juce::TableHeaderComponent::textColourId, tokens::fgPrimary);

    // ----- ListBox (most JUCE list views) ----------------------------------
    setColour(juce::ListBox::backgroundColourId, tokens::bgWindow);
    setColour(juce::ListBox::outlineColourId, tokens::borderDefault);
    setColour(juce::ListBox::textColourId, tokens::fgPrimary);

    // ----- ResizableWindow / DocumentWindow chrome -------------------------
    setColour(juce::ResizableWindow::backgroundColourId, tokens::bgWindow);
    setColour(juce::DocumentWindow::textColourId, tokens::fgPrimary);

    // ----- AlertWindow ------------------------------------------------------
    setColour(juce::AlertWindow::backgroundColourId, tokens::bgPopover);
    setColour(juce::AlertWindow::textColourId, tokens::fgPrimary);
    setColour(juce::AlertWindow::outlineColourId, tokens::borderDefault);

    // ----- ProgressBar (plugin scan, future patch loading) -----------------
    setColour(juce::ProgressBar::backgroundColourId, tokens::bgSecondary);
    setColour(juce::ProgressBar::foregroundColourId, tokens::accentPrimary);

    // ----- AudioDeviceSelectorComponent already routes via the slider/
    //       combo/label/text-editor IDs above. No bespoke overrides needed.
}

void Pb3LookAndFeel::drawButtonBackground(juce::Graphics& g,
                                          juce::Button& button,
                                          const juce::Colour& backgroundColour,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker(0.15f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.10f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, kButtonCornerRadius);

    g.setColour(button.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, kButtonCornerRadius, 1.0f);
}

void Pb3LookAndFeel::drawTickBox(juce::Graphics& g,
                                 juce::Component& component,
                                 float x,
                                 float y,
                                 float w,
                                 float h,
                                 bool ticked,
                                 bool isEnabled,
                                 bool /*shouldDrawButtonAsHighlighted*/,
                                 bool /*shouldDrawButtonAsDown*/)
{
    juce::Rectangle<float> box(x, y, w, h);

    g.setColour(tokens::bgSecondary);
    g.fillRoundedRectangle(box, kTickBoxCornerRadius);

    g.setColour(tokens::borderDefault);
    g.drawRoundedRectangle(box, kTickBoxCornerRadius, 1.0f);

    if (ticked)
    {
        const auto fill = isEnabled ? component.findColour(juce::ToggleButton::tickColourId)
                                    : component.findColour(juce::ToggleButton::tickDisabledColourId);
        g.setColour(fill);
        g.fillRoundedRectangle(box.reduced(2.5f), kTickBoxCornerRadius - 1.0f);
    }
}

} // namespace pedalboard3::ui
