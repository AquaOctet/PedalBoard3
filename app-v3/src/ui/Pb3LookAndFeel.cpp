#include "Pb3LookAndFeel.h"

#include "ColourScheme.h"
#include "DialogFrame.h"
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
    // borderMuted, not borderDefault: the canvas's horizontal bar runs a few
    // pixels above the footer's top edge, and in the border colour the two
    // read as one doubled line rather than as a scrollbar.
    setColour(juce::ScrollBar::backgroundColourId, tokens::bgWindow);
    setColour(juce::ScrollBar::thumbColourId, tokens::borderMuted);
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
    setColour(juce::AlertWindow::backgroundColourId, tokens::bgWindow);
    setColour(juce::AlertWindow::textColourId, tokens::fgSecondaryText);
    setColour(juce::AlertWindow::outlineColourId, tokens::borderDefault);

    // ----- ProgressBar (plugin scan, future patch loading) -----------------
    setColour(juce::ProgressBar::backgroundColourId, tokens::bgSecondary);
    setColour(juce::ProgressBar::foregroundColourId, tokens::accentPrimary);
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

namespace
{

// The dialog title bar's close cross (UI chrome U4): fgSubtle, lit on hover.
class DialogCloseButton : public juce::Button
{
public:
    DialogCloseButton() : juce::Button("Close") {}

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        const bool lit = shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown;
        const auto area = getLocalBounds().toFloat();
        if (lit)
        {
            g.setColour(tokens::bgSecondary);
            g.fillRoundedRectangle(area.reduced(4.0f), 4.0f);
        }
        const auto cross = area.withSizeKeepingCentre(8.0f, 8.0f);
        juce::Path path;
        path.addLineSegment({cross.getTopLeft(), cross.getBottomRight()}, 1.4f);
        path.addLineSegment({cross.getTopRight(), cross.getBottomLeft()}, 1.4f);
        g.setColour(lit ? tokens::fgPrimary : tokens::fgSubtle);
        g.fillPath(path);
    }
};

} // namespace

void Pb3LookAndFeel::drawDocumentWindowTitleBar(juce::DocumentWindow& window,
                                                juce::Graphics& g,
                                                int w,
                                                int h,
                                                int /*titleSpaceX*/,
                                                int /*titleSpaceW*/,
                                                const juce::Image* /*icon*/,
                                                bool /*drawTitleTextOnLeft*/)
{
    g.setColour(tokens::bgTitleBar);
    g.fillAll();
    const auto layout = dialogframe::titleBarLayout({0, 0, w, h});
    g.setFont(fonts::ui(13.0f, fonts::Weight::Medium));
    g.setColour(tokens::fgTitle);
    g.drawText(window.getName(), layout.title, juce::Justification::centredLeft, true);
}

juce::Button* Pb3LookAndFeel::createDocumentWindowButton(int buttonType)
{
    if (buttonType == juce::DocumentWindow::closeButton)
        return new DialogCloseButton();
    return juce::LookAndFeel_V4::createDocumentWindowButton(buttonType);
}

void Pb3LookAndFeel::positionDocumentWindowButtons(juce::DocumentWindow& window,
                                                   int titleBarX,
                                                   int titleBarY,
                                                   int titleBarW,
                                                   int titleBarH,
                                                   juce::Button* minimiseButton,
                                                   juce::Button* maximiseButton,
                                                   juce::Button* closeButton,
                                                   bool positionTitleBarButtonsOnLeft)
{
    // Dialogs have only the close button; anything else keeps V4's layout.
    if (minimiseButton != nullptr || maximiseButton != nullptr)
    {
        juce::LookAndFeel_V4::positionDocumentWindowButtons(window,
                                                            titleBarX,
                                                            titleBarY,
                                                            titleBarW,
                                                            titleBarH,
                                                            minimiseButton,
                                                            maximiseButton,
                                                            closeButton,
                                                            positionTitleBarButtonsOnLeft);
        return;
    }
    if (closeButton != nullptr)
        closeButton->setBounds(dialogframe::titleBarLayout({titleBarX, titleBarY, titleBarW, titleBarH}).close);
}

void Pb3LookAndFeel::drawResizableWindowBorder(
    juce::Graphics& g, int w, int h, const juce::BorderSize<int>& border, juce::ResizableWindow& window)
{
    if (border.isEmpty())
        return; // a native title bar: the system draws the frame

    // A resizable dialog's border is 4 px: the strip above and beside the title
    // bar takes its colour, so the bar reaches the edge.
    if (auto* document = dynamic_cast<juce::DocumentWindow*>(&window))
    {
        g.setColour(tokens::bgTitleBar);
        g.fillRect(0, 0, w, border.getTop() + document->getTitleBarHeight());
    }
    g.setColour(juce::Colours::black);
    g.drawRect(0, 0, w, h, 1);
}

void Pb3LookAndFeel::drawAlertBox(juce::Graphics& g,
                                  juce::AlertWindow& alert,
                                  const juce::Rectangle<int>& textArea,
                                  juce::TextLayout& textLayout)
{
    g.fillAll(alert.findColour(juce::AlertWindow::backgroundColourId));
    g.setColour(juce::Colours::black);
    g.drawRect(alert.getLocalBounds(), 1);
    textLayout.draw(g, textArea.toFloat());
}

juce::Font Pb3LookAndFeel::getAlertWindowTitleFont()
{
    return fonts::ui(15.0f, fonts::Weight::SemiBold);
}

juce::Font Pb3LookAndFeel::getAlertWindowMessageFont()
{
    return fonts::ui(14.0f);
}

juce::Font Pb3LookAndFeel::getAlertWindowFont()
{
    return fonts::ui(13.0f);
}

int Pb3LookAndFeel::getAlertWindowButtonHeight()
{
    return 30;
}

void Pb3LookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&,
                                              juce::Graphics& g,
                                              const juce::Path& path,
                                              juce::Image&)
{
    g.setColour(tokens::bgPopover);
    g.fillPath(path);
    g.setColour(tokens::borderDefault);
    g.strokePath(path, juce::PathStrokeType(1.0f));
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

void Pb3LookAndFeel::drawScrollbar(juce::Graphics& g,
                                   juce::ScrollBar& bar,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   bool isScrollbarVertical,
                                   int thumbStartPosition,
                                   int thumbSize,
                                   bool isMouseOver,
                                   bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    // Thin and rounded, with no track; a little wider under the mouse.
    const float thickness = (isMouseOver || isMouseDown) ? 6.0f : 4.0f;
    auto thumb = isScrollbarVertical ? juce::Rectangle<int>(x, thumbStartPosition, width, thumbSize).toFloat()
                                     : juce::Rectangle<int>(thumbStartPosition, y, thumbSize, height).toFloat();
    thumb = isScrollbarVertical ? thumb.withSizeKeepingCentre(thickness, thumb.getHeight() - 4.0f)
                                : thumb.withSizeKeepingCentre(thumb.getWidth() - 4.0f, thickness);
    const auto colour = bar.findColour(juce::ScrollBar::thumbColourId);
    g.setColour((isMouseOver || isMouseDown) ? colour.brighter(0.3f) : colour);
    g.fillRoundedRectangle(thumb, thickness * 0.5f);
}

// ---- Pb3DialogLookAndFeel (UI chrome U4) ------------------------------------

namespace
{
constexpr float kFieldRadius = 5.0f;
constexpr float kDialogButtonRadius = 6.0f;
constexpr float kFieldFontSize = 12.0f;
constexpr float kTabCaptionSize = 12.0f;
constexpr float kToggleOnAlpha = 0.14f;

juce::Colour pressedOrHovered(juce::Colour fill, bool highlighted, bool down)
{
    if (down)
        return fill.overlaidWith(tokens::fgPrimary.withAlpha(0.08f));
    if (highlighted)
        return fill.overlaidWith(tokens::fgPrimary.withAlpha(0.05f));
    return fill;
}
} // namespace

Pb3DialogLookAndFeel::Pb3DialogLookAndFeel()
{
    setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
    setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary.withAlpha(kToggleOnAlpha));
    setColour(juce::TextButton::textColourOffId, tokens::fgPrimary);
    setColour(juce::TextButton::textColourOnId, tokens::accentPrimary);
    setColour(juce::ToggleButton::textColourId, tokens::fgField);

    setColour(juce::ComboBox::backgroundColourId, tokens::bgSecondary);
    setColour(juce::ComboBox::textColourId, tokens::fgField);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, tokens::fgSubtle);

    setColour(juce::TextEditor::backgroundColourId, tokens::bgSecondary);
    setColour(juce::TextEditor::textColourId, tokens::fgField);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);

    setColour(juce::Slider::textBoxBackgroundColourId, tokens::bgSecondary);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxTextColourId, tokens::fgField);

    setColour(juce::ScrollBar::thumbColourId, tokens::borderDefault);

    setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);

    // Tables, lists and trees (UI chrome U7). JUCE's Plugin List shades a
    // selected row halfway between these two.
    setColour(juce::ListBox::backgroundColourId, tokens::bgWindow);
    setColour(juce::ListBox::textColourId, tokens::fgMenu);
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TableHeaderComponent::backgroundColourId, tokens::bgWindow);
    setColour(juce::TableHeaderComponent::textColourId, tokens::fgSubtle);
    setColour(juce::TableHeaderComponent::outlineColourId, tokens::borderDefault);
    setColour(juce::TableHeaderComponent::highlightColourId, tokens::bgSecondary);
    setColour(juce::TreeView::backgroundColourId, tokens::bgWindow);
    setColour(juce::TreeView::linesColourId, tokens::borderDefault);
    setColour(juce::TreeView::selectedItemBackgroundColourId, tokens::bgSecondary);
    setColour(juce::TreeView::oddItemsColourId, juce::Colours::transparentBlack);
    setColour(juce::TreeView::evenItemsColourId, juce::Colours::transparentBlack);
    setColour(juce::KeyMappingEditorComponent::backgroundColourId, tokens::bgWindow);
    setColour(juce::KeyMappingEditorComponent::textColourId, tokens::fgMenu);
}

void Pb3DialogLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool shouldDrawButtonAsHighlighted,
                                                bool shouldDrawButtonAsDown)
{
    // Filled bgSecondary, or a ghost (transparent with a 1 px edge, teal when
    // toggled on); no teal-filled primary.
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool ghost = static_cast<bool>(button.getProperties()[kOutlineButton]);
    const bool on = button.getToggleState();

    auto fill = (ghost && !on) ? juce::Colours::transparentBlack : backgroundColour;
    fill = pressedOrHovered(fill, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    g.setColour(fill.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle(bounds, kDialogButtonRadius);

    if (ghost)
    {
        g.setColour(on ? tokens::accentPrimary : tokens::borderDefault);
        g.drawRoundedRectangle(bounds, kDialogButtonRadius, 1.0f);
    }
}

juce::Font Pb3DialogLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return fonts::ui(juce::jmin(14.0f, static_cast<float>(buttonHeight) * 0.6f), fonts::Weight::Medium);
}

void Pb3DialogLookAndFeel::drawToggleButton(juce::Graphics& g,
                                            juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    // A toggle as a field: bgSecondary, or teal at 14% with teal text when on,
    // with its tick box at the left.
    const bool on = button.getToggleState();
    const float alpha = button.isEnabled() ? 1.0f : 0.5f;
    auto fill = on ? tokens::accentPrimary.withAlpha(kToggleOnAlpha) : tokens::bgSecondary;
    fill = pressedOrHovered(fill, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(0.5f), kFieldRadius);

    auto content = button.getLocalBounds().reduced(10, 0);
    const auto box = content.removeFromLeft(14).withSizeKeepingCentre(14, 14).toFloat();
    drawTickBox(g,
                button,
                box.getX(),
                box.getY(),
                box.getWidth(),
                box.getHeight(),
                on,
                button.isEnabled(),
                shouldDrawButtonAsHighlighted,
                shouldDrawButtonAsDown);
    content.removeFromLeft(8);

    const auto textColour = on ? tokens::accentPrimary : button.findColour(juce::ToggleButton::textColourId);
    g.setFont(fonts::ui(juce::jmin(14.0f, static_cast<float>(button.getHeight()) * 0.6f)));
    g.setColour(textColour.withMultipliedAlpha(alpha));
    g.drawFittedText(button.getButtonText(), content, juce::Justification::centredLeft, 2);
}

void Pb3DialogLookAndFeel::drawComboBox(juce::Graphics& g,
                                        int width,
                                        int height,
                                        bool /*isButtonDown*/,
                                        int /*buttonX*/,
                                        int /*buttonY*/,
                                        int /*buttonW*/,
                                        int /*buttonH*/,
                                        juce::ComboBox& box)
{
    const float alpha = box.isEnabled() ? 1.0f : 0.5f;
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(juce::Rectangle<int>(width, height).toFloat(), kFieldRadius);

    // The mockup's " ▾", drawn so it doesn't depend on the font.
    const float centreX = static_cast<float>(width) - 14.0f;
    const float centreY = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(centreX - 4.0f, centreY - 2.0f, centreX + 4.0f, centreY - 2.0f, centreX, centreY + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(alpha));
    g.fillPath(arrow);
}

juce::Font Pb3DialogLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return fonts::ui(kFieldFontSize);
}

void Pb3DialogLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    // The label's own 5 px inset puts the text 10 px in; the arrow keeps 24.
    label.setBounds(5, 1, box.getWidth() - 5 - 24, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void Pb3DialogLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<int>(width, height).toFloat(), kFieldRadius);
}

void Pb3DialogLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    // No border, except a teal edge while typing.
    if (editor.isEnabled() && !editor.isReadOnly() && editor.hasKeyboardFocus(true))
    {
        g.setColour(tokens::accentPrimary.withAlpha(0.6f));
        g.drawRoundedRectangle(juce::Rectangle<int>(width, height).toFloat().reduced(0.5f), kFieldRadius, 1.0f);
    }
}

void Pb3DialogLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                            int x,
                                            int y,
                                            int width,
                                            int height,
                                            float sliderPos,
                                            float minSliderPos,
                                            float maxSliderPos,
                                            juce::Slider::SliderStyle style,
                                            juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(
            g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    // The mockup's value bar: a field filled teal up to the value, with a teal
    // edge at the value.
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto track = area.withSizeKeepingCentre(area.getWidth(), juce::jmin(area.getHeight(), 28.0f));
    const float alpha = slider.isEnabled() ? 1.0f : 0.5f;
    g.setColour(tokens::bgSecondary.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, kFieldRadius);

    const float value = juce::jlimit(track.getX(), track.getRight(), sliderPos);
    g.setColour(tokens::accentPrimary.withAlpha(0.28f * alpha));
    g.fillRoundedRectangle(track.withRight(value), kFieldRadius);
    g.setColour(tokens::accentPrimary.withMultipliedAlpha(alpha));
    g.fillRect(juce::Rectangle<float>(value - 1.0f, track.getY(), 2.0f, track.getHeight()));
}

int Pb3DialogLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int /*tabDepth*/)
{
    return juce::GlyphArrangement::getStringWidthInt(fonts::caption(kTabCaptionSize),
                                                     button.getButtonText().toUpperCase()) +
           28;
}

void Pb3DialogLookAndFeel::drawTabButton(juce::TabBarButton& button,
                                         juce::Graphics& g,
                                         bool isMouseOver,
                                         bool /*isMouseDown*/)
{
    // An uppercase caption; the front tab is lit and underlined in teal.
    const bool front = button.isFrontTab();
    auto area = button.getLocalBounds();
    g.setFont(fonts::caption(kTabCaptionSize));
    g.setColour(front ? tokens::fgPrimary : (isMouseOver ? tokens::fgMenu : tokens::fgSubtle));
    g.drawText(button.getButtonText().toUpperCase(), area, juce::Justification::centred, false);
    if (front)
    {
        g.setColour(tokens::accentPrimary);
        g.fillRect(area.removeFromBottom(2).reduced(10, 0));
    }
}

void Pb3DialogLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);
    g.setColour(tokens::borderDefault);
    g.fillRect(bar.getLocalBounds().removeFromBottom(1));
}

void Pb3DialogLookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

void Pb3DialogLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll(header.findColour(juce::TableHeaderComponent::backgroundColourId));
    g.setColour(header.findColour(juce::TableHeaderComponent::outlineColourId));
    g.fillRect(header.getLocalBounds().removeFromBottom(1));
}

void Pb3DialogLookAndFeel::drawTableHeaderColumn(juce::Graphics& g,
                                                 juce::TableHeaderComponent& header,
                                                 const juce::String& columnName,
                                                 int /*columnId*/,
                                                 int width,
                                                 int height,
                                                 bool isMouseOver,
                                                 bool isMouseDown,
                                                 int columnFlags)
{
    if (isMouseDown || isMouseOver)
        g.fillAll(header.findColour(juce::TableHeaderComponent::highlightColourId)
                      .withMultipliedAlpha(isMouseDown ? 1.0f : 0.6f));

    auto area = juce::Rectangle<int>(width, height).reduced(10, 0).withTrimmedBottom(1);
    const bool forwards = (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0;
    const bool sorted = forwards || (columnFlags & juce::TableHeaderComponent::sortedBackwards) != 0;
    if (sorted)
    {
        // A small triangle: up sorted forwards, down backwards.
        const auto box = area.removeFromRight(10).toFloat().withSizeKeepingCentre(7.0f, 4.0f);
        juce::Path arrow;
        if (forwards)
            arrow.addTriangle(
                box.getX(), box.getBottom(), box.getRight(), box.getBottom(), box.getCentreX(), box.getY());
        else
            arrow.addTriangle(box.getX(), box.getY(), box.getRight(), box.getY(), box.getCentreX(), box.getBottom());
        g.setColour(tokens::fgSubtle);
        g.fillPath(arrow);
    }

    g.setFont(fonts::caption(11.0f));
    g.setColour(sorted ? tokens::fgMenu : header.findColour(juce::TableHeaderComponent::textColourId));
    g.drawFittedText(columnName.toUpperCase(), area, juce::Justification::centredLeft, 1);
}

void Pb3DialogLookAndFeel::drawKeymapChangeButton(
    juce::Graphics& g, int width, int height, juce::Button& button, const juce::String& keyDescription)
{
    const bool lit = button.isEnabled() && (button.isOver() || button.isDown());
    const auto area = juce::Rectangle<int>(width, height).toFloat().reduced(1.0f, 2.0f);

    if (keyDescription.isNotEmpty())
    {
        // A key as a small field, in mono.
        g.setColour(lit ? tokens::bgSecondary.brighter(0.08f) : tokens::bgSecondary);
        g.fillRoundedRectangle(area, 4.0f);
        g.setFont(fonts::mono(11.0f));
        g.setColour(tokens::fgField.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText(keyDescription, area.toNearestInt().reduced(4, 0), juce::Justification::centred, 1);
    }
    else
    {
        // Add a key: a quiet plus.
        if (lit)
        {
            g.setColour(tokens::bgSecondary);
            g.fillRoundedRectangle(area, 4.0f);
        }
        const auto plus = area.withSizeKeepingCentre(9.0f, 9.0f);
        juce::Path path;
        path.addLineSegment({plus.getCentreX(), plus.getY(), plus.getCentreX(), plus.getBottom()}, 1.4f);
        path.addLineSegment({plus.getX(), plus.getCentreY(), plus.getRight(), plus.getCentreY()}, 1.4f);
        g.setColour(lit ? tokens::fgPrimary : tokens::fgSubtle);
        g.fillPath(path);
    }

    if (button.hasKeyboardFocus(false))
    {
        g.setColour(tokens::accentPrimary.withAlpha(0.6f));
        g.drawRoundedRectangle(area, 4.0f, 1.0f);
    }
}

void Pb3DialogLookAndFeel::drawTreeviewPlusMinusBox(juce::Graphics& g,
                                                    const juce::Rectangle<float>& area,
                                                    juce::Colour /*backgroundColour*/,
                                                    bool isOpen,
                                                    bool isMouseOver)
{
    // A chevron, pointing right while closed and down while open.
    const auto box = area.withSizeKeepingCentre(8.0f, 8.0f);
    juce::Path chevron;
    if (isOpen)
        chevron.addTriangle(
            box.getX(), box.getY() + 2.0f, box.getRight(), box.getY() + 2.0f, box.getCentreX(), box.getBottom() - 1.0f);
    else
        chevron.addTriangle(
            box.getX() + 2.0f, box.getY(), box.getX() + 2.0f, box.getBottom(), box.getRight() - 1.0f, box.getCentreY());
    g.setColour(isMouseOver ? tokens::fgPrimary : tokens::fgSubtle);
    g.fillPath(chevron);
}

bool Pb3DialogLookAndFeel::areLinesDrawnForTreeView(juce::TreeView&)
{
    return false;
}

} // namespace pedalboard3::ui
