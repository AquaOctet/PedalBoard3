#include "Pb3LookAndFeel.h"

#include "ColourScheme.h"
#include "Fonts.h"

namespace pedalboard3::ui
{

namespace
{
constexpr float kButtonCornerRadius = 4.0f;
constexpr float kTickBoxCornerRadius = 3.0f;

// Menus (UI chrome U3), from the mockup's .menu.
constexpr int kMenuBorder = 5; // the panel's padding
constexpr float kMenuCornerRadius = 7.0f;
constexpr float kMenuItemCornerRadius = 5.0f;
constexpr int kMenuItemHeight = 30;
constexpr int kMenuSeparatorHeight = 9;
constexpr int kMenuHeaderHeight = 16; // JUCE adds half again: 24
constexpr int kMenuItemPadding = 10;
constexpr int kMenuGutter = 16;     // the tick or icon column
constexpr int kMenuArrowWidth = 10; // the submenu marker
constexpr int kMenuAccessoryGap = 16;
constexpr int kMenuSeparatorInset = 6;
constexpr float kMenuFontSize = 14.0f;
constexpr float kMenuAccessorySize = 10.0f;
constexpr float kMenuHeaderSize = 11.0f;
// JUCE measures an item as its text, three spaces and its shortcut
// (juce_PopupMenu.cpp, ItemComponent::getTextForMeasurement).
constexpr const char* kMeasureSeparator = "   ";

juce::Font menuAccessoryFont()
{
    return fonts::mono(kMenuAccessorySize);
}

juce::Font menuHeaderFont()
{
    return fonts::caption(kMenuHeaderSize);
}
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

    // Fonts: the embedded Barlow and JetBrains Mono (getTypefaceForFont), on
    // every platform.

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

    // ----- PopupMenu (UI chrome U3) -----------------------------------------
    // The background is a shade short of opaque, so JUCE gives menus a
    // see-through window (juce_PopupMenu.cpp, MenuWindow) and the panel's
    // rounded corners stay clear; drawPopupMenuBackgroundWithOptions paints it.
    setColour(juce::PopupMenu::backgroundColourId, tokens::bgPopover.withAlpha(0.99f));
    setColour(juce::PopupMenu::textColourId, tokens::fgMenu);
    setColour(juce::PopupMenu::headerTextColourId, tokens::fgSubtle);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, tokens::bgSecondary);
    setColour(juce::PopupMenu::highlightedTextColourId, tokens::fgPrimary);

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

juce::Typeface::Ptr Pb3LookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    const auto name = font.getTypefaceName();
    if (name == juce::Font::getDefaultSansSerifFontName())
        return fonts::uiTypeface(font.isBold() ? fonts::Weight::SemiBold : fonts::Weight::Regular);
    if (name == juce::Font::getDefaultMonospacedFontName())
        return fonts::monoTypeface(font.isBold() ? fonts::Weight::Medium : fonts::Weight::Regular);
    return juce::LookAndFeel_V4::getTypefaceForFont(font);
}

int Pb3LookAndFeel::getPopupMenuBorderSize()
{
    return kMenuBorder;
}

int Pb3LookAndFeel::getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&)
{
    return kMenuBorder;
}

juce::Font Pb3LookAndFeel::getPopupMenuFont()
{
    return fonts::ui(kMenuFontSize);
}

void Pb3LookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                                        int width,
                                                        int height,
                                                        const juce::PopupMenu::Options&)
{
    const auto panel =
        juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(tokens::bgPopover);
    g.fillRoundedRectangle(panel, kMenuCornerRadius);
    g.setColour(tokens::borderDefault);
    g.drawRoundedRectangle(panel, kMenuCornerRadius, 1.0f);
}

void Pb3LookAndFeel::getIdealPopupMenuItemSizeWithOptions(const juce::String& text,
                                                          bool isSeparator,
                                                          int standardMenuItemHeight,
                                                          int& idealWidth,
                                                          int& idealHeight,
                                                          const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = kMenuSeparatorHeight;
        return;
    }

    // JUCE measures a section header with a standard height of -1
    // (juce_PopupMenu.cpp, HeaderItemComponent), then adds half again to the
    // height and a quarter to the width.
    if (standardMenuItemHeight < 0)
    {
        idealWidth =
            kMenuItemPadding * 2 + juce::GlyphArrangement::getStringWidthInt(menuHeaderFont(), text.toUpperCase());
        idealHeight = kMenuHeaderHeight;
        return;
    }

    // Every item is a mockup row, ComboBox lists too. The width leaves room for
    // the gutter, the submenu marker and the accessory in its own font.
    const auto label = text.upToFirstOccurrenceOf(kMeasureSeparator, false, false);
    const auto accessory = text.fromFirstOccurrenceOf(kMeasureSeparator, false, false).trim();
    idealWidth = kMenuItemPadding * 2 + kMenuGutter + kMenuArrowWidth +
                 juce::GlyphArrangement::getStringWidthInt(getPopupMenuFont(), label);
    if (accessory.isNotEmpty())
        idealWidth += kMenuAccessoryGap + juce::GlyphArrangement::getStringWidthInt(menuAccessoryFont(), accessory);
    idealHeight = kMenuItemHeight;
}

void Pb3LookAndFeel::drawPopupMenuItemWithOptions(juce::Graphics& g,
                                                  const juce::Rectangle<int>& area,
                                                  bool isHighlighted,
                                                  const juce::PopupMenu::Item& item,
                                                  const juce::PopupMenu::Options& options)
{
    // An item spans the menu's whole width; the panel's padding is the border.
    auto row = area.reduced(getPopupMenuBorderSizeWithOptions(options), 0);

    if (item.isSeparator)
    {
        g.setColour(tokens::borderDefault);
        g.fillRect(row.reduced(kMenuSeparatorInset, 0).withHeight(1).withY(row.getCentreY()));
        return;
    }

    const bool highlighted = isHighlighted && item.isEnabled;
    if (highlighted)
    {
        g.setColour(tokens::bgSecondary);
        g.fillRoundedRectangle(row.toFloat(), kMenuItemCornerRadius);
    }

    auto textColour = highlighted ? tokens::fgPrimary : tokens::fgMenu;
    if (item.colour != juce::Colour())
        textColour = item.colour;
    if (!item.isEnabled)
        textColour = tokens::fgSubtle;

    auto content = row.reduced(kMenuItemPadding, 0);

    const auto gutter = content.removeFromLeft(kMenuGutter).toFloat();
    if (item.image != nullptr)
    {
        item.image->drawWithin(g,
                               gutter.reduced(1.0f),
                               juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                               1.0f);
    }
    else if (item.isTicked)
    {
        const auto tick = getTickShape(1.0f);
        const juce::Rectangle<float> tickArea(gutter.getX(), gutter.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour(item.isEnabled ? tokens::accentPrimary : tokens::fgSubtle);
        g.fillPath(tick, tick.getTransformToScaleToFit(tickArea, true));
    }

    const bool hasSubMenu = item.subMenu != nullptr && (item.itemID == 0 || item.subMenu->getNumItems() > 0);
    if (hasSubMenu)
    {
        // The mockup's ▸, drawn so it doesn't depend on the font.
        const auto marker = content.removeFromRight(kMenuArrowWidth).toFloat();
        const float markerWidth = 4.5f;
        const float markerHeight = 7.0f;
        const float x = marker.getRight() - markerWidth;
        const float y = marker.getCentreY();
        juce::Path path;
        path.addTriangle(x, y - markerHeight * 0.5f, x + markerWidth, y, x, y + markerHeight * 0.5f);
        g.setColour(highlighted ? tokens::fgPrimary : tokens::fgSubtle);
        g.fillPath(path);
        content.removeFromRight(4);
    }

    if (item.shortcutKeyDescription.isNotEmpty())
    {
        const auto font = menuAccessoryFont();
        const int accessoryWidth = juce::GlyphArrangement::getStringWidthInt(font, item.shortcutKeyDescription) + 2;
        g.setFont(font);
        g.setColour(tokens::fgSubtle);
        g.drawText(item.shortcutKeyDescription,
                   content.removeFromRight(accessoryWidth),
                   juce::Justification::centredRight,
                   false);
        content.removeFromRight(kMenuAccessoryGap);
    }

    g.setFont(getPopupMenuFont());
    g.setColour(textColour);
    g.drawFittedText(item.text, content, juce::Justification::centredLeft, 1);
}

void Pb3LookAndFeel::drawPopupMenuSectionHeaderWithOptions(juce::Graphics& g,
                                                           const juce::Rectangle<int>& area,
                                                           const juce::String& sectionName,
                                                           const juce::PopupMenu::Options&)
{
    // JUCE has already inset the area by the border. The mockup's padding: 10 at
    // the sides and 3 below, the text sitting on the bottom.
    g.setFont(menuHeaderFont());
    g.setColour(tokens::fgSubtle);
    g.drawFittedText(sectionName.toUpperCase(),
                     area.reduced(kMenuItemPadding, 0).withTrimmedBottom(3),
                     juce::Justification::bottomLeft,
                     1);
}

void Pb3LookAndFeel::drawPopupMenuUpDownArrowWithOptions(
    juce::Graphics& g, int width, int height, bool isScrollUpArrow, const juce::PopupMenu::Options&)
{
    const auto w = static_cast<float>(width);
    const auto h = static_cast<float>(height);
    g.setColour(tokens::bgPopover);
    g.fillRect(1.0f, 1.0f, w - 2.0f, h - 2.0f);

    const float halfWidth = h * 0.25f;
    const float y1 = h * (isScrollUpArrow ? 0.6f : 0.4f);
    const float y2 = h * (isScrollUpArrow ? 0.4f : 0.6f);
    juce::Path path;
    path.addTriangle(w * 0.5f - halfWidth, y1, w * 0.5f + halfWidth, y1, w * 0.5f, y2);
    g.setColour(tokens::fgSubtle);
    g.fillPath(path);
}

void Pb3LookAndFeel::drawButtonBackground(juce::Graphics& g,
                                          juce::Button& button,
                                          const juce::Colour& backgroundColour,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

    if (static_cast<bool>(button.getProperties()[kOutlineButton]))
    {
        // Outline button: no fill until toggled on, then a teal wash and edge.
        const bool on = button.getToggleState();
        auto wash = on ? tokens::accentPrimary.withAlpha(0.14f) : juce::Colours::transparentBlack;
        if (shouldDrawButtonAsDown)
            wash = wash.overlaidWith(tokens::fgPrimary.withAlpha(0.08f));
        else if (shouldDrawButtonAsHighlighted)
            wash = wash.overlaidWith(tokens::fgPrimary.withAlpha(0.05f));
        g.setColour(wash);
        g.fillRoundedRectangle(bounds, kButtonCornerRadius);
        g.setColour(on ? tokens::accentPrimary : tokens::borderDefault);
        g.drawRoundedRectangle(bounds, kButtonCornerRadius, 1.0f);
        return;
    }

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
