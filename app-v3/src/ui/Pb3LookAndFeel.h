#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace pedalboard3::ui
{

// Pedalboard 3 LookAndFeel — subclass of juce::LookAndFeel_V4 that
// replaces the JUCE-default-grey aesthetic with the dark blue-gray +
// teal-accent palette from the Vercel design mockup
// (vercel-ui-demo/app/globals.css).
//
// Theming strategy
// ----------------
// JUCE's V4 LookAndFeel reads a `ColourScheme` of 9 named slots (window
// background, widget background, menu background, outline, text, fill,
// highlighted text, highlighted fill, menu text). Setting those once
// drives most JUCE classes' default `paint*` methods to the right
// colours without bespoke overrides.
//
// For the long-tail JUCE classes that ignore the V4 `ColourScheme`
// (or that the mockup wants styled differently — sliders, scrollbars,
// table headers, popup menus, toggle buttons, plugin-list view), we
// register per-class colour IDs via `setColour(ClassName::ColourId,
// tokens::…)` in the constructor.
//
// Bespoke `paint*` overrides draw what colours alone can't: rounded buttons,
// the popup menus, the dialog title bar and its close cross, alerts, the
// call-out box, and the embedded fonts (getTypefaceForFont). Controls inside
// dialogs take the mockup's look from Pb3DialogLookAndFeel below, which each
// DialogFrame installs.
//
// Singleton lifetime
// ------------------
// One instance is owned by `App` and registered via
// `juce::LookAndFeel::setDefaultLookAndFeel(&lnf)` in `App::initialise`.
// `App::shutdown` calls `setDefaultLookAndFeel(nullptr)` BEFORE
// destroying the instance so `juce::Desktop` doesn't hold a dangling
// pointer.
class Pb3LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Pb3LookAndFeel();
    ~Pb3LookAndFeel() override = default;

    // A component property: set it true on a Button to draw it as an outline
    // ("ghost") button, transparent until toggled on (UI refresh U2).
    static constexpr const char* kOutlineButton = "pb3OutlineButton";

    // Bespoke shape overrides — rounded button corners + a thin
    // accent border on default buttons. All colour decisions still
    // route through the V4 ColourScheme + per-class setColour calls
    // configured in the constructor.
    void drawButtonBackground(juce::Graphics&,
                              juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    // Filled circular tick instead of V4's rounded-rect-with-tick.
    // Used by ToggleButtons (the per-card Bypass control etc.).
    void drawTickBox(juce::Graphics&,
                     juce::Component&,
                     float x,
                     float y,
                     float w,
                     float h,
                     bool ticked,
                     bool isEnabled,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    // Menus and submenus, ComboBox lists included, as the mockup's .menu (UI
    // chrome plan U3): a rounded panel with 5 px of padding, 30 px rows with a
    // rounded bgSecondary highlight, ticks in a teal left gutter, the shortcut or
    // format tag on the right in mono, and uppercase Barlow Condensed headers.
    // A menu opened from a button is at least kMenuMinimumWidth wide and opens
    // kMenuGap below it.
    static constexpr int kMenuMinimumWidth = 208;
    static constexpr int kMenuGap = 6;

    int getPopupMenuBorderSize() override;
    int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override;
    void drawPopupMenuBackgroundWithOptions(juce::Graphics&,
                                            int width,
                                            int height,
                                            const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions(juce::Graphics&,
                                      const juce::Rectangle<int>& area,
                                      bool isHighlighted,
                                      const juce::PopupMenu::Item& item,
                                      const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions(juce::Graphics&,
                                               const juce::Rectangle<int>& area,
                                               const juce::String& sectionName,
                                               const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions(const juce::String& text,
                                              bool isSeparator,
                                              int standardMenuItemHeight,
                                              int& idealWidth,
                                              int& idealHeight,
                                              const juce::PopupMenu::Options&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuUpDownArrowWithOptions(
        juce::Graphics&, int width, int height, bool isScrollUpArrow, const juce::PopupMenu::Options&) override;

    // The dialog frame (UI chrome U4, ui/DialogFrame.h): a 28 px bgTitleBar title
    // bar with the title on the left, a close cross, and a 1 px black edge.
    // Only windows without a native title bar reach these, and those are the
    // dialogs. The main window and plugin editors use the native one.
    void drawDocumentWindowTitleBar(juce::DocumentWindow&,
                                    juce::Graphics&,
                                    int w,
                                    int h,
                                    int titleSpaceX,
                                    int titleSpaceW,
                                    const juce::Image* icon,
                                    bool drawTitleTextOnLeft) override;
    juce::Button* createDocumentWindowButton(int buttonType) override;
    void positionDocumentWindowButtons(juce::DocumentWindow&,
                                       int titleBarX,
                                       int titleBarY,
                                       int titleBarW,
                                       int titleBarH,
                                       juce::Button* minimiseButton,
                                       juce::Button* maximiseButton,
                                       juce::Button* closeButton,
                                       bool positionTitleBarButtonsOnLeft) override;
    void drawResizableWindowBorder(
        juce::Graphics&, int w, int h, const juce::BorderSize<int>& border, juce::ResizableWindow&) override;

    // The alerts JUCE still raises itself, such as the plugin scan's progress
    // window (UI chrome U5): the dialog body and edge, no icon, Barlow. The app's
    // own alerts are ui::alert.
    void drawAlertBox(juce::Graphics&,
                      juce::AlertWindow&,
                      const juce::Rectangle<int>& textArea,
                      juce::TextLayout&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    int getAlertWindowButtonHeight() override;

    // Popovers (the tempo pill's): the menu panel's colours and edge.
    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;

    // Scrollbars, everywhere there is one: a thin rounded thumb and no track.
    // V4 draws a 6 px bar the width of the viewport in whatever the thumb
    // colour is, which above the footer reads as a second border rather than
    // as something you can drag.
    void drawScrollbar(juce::Graphics&,
                       juce::ScrollBar&,
                       int x,
                       int y,
                       int width,
                       int height,
                       bool isScrollbarVertical,
                       int thumbStartPosition,
                       int thumbSize,
                       bool isMouseOver,
                       bool isMouseDown) override;

    // The mockup's embedded fonts stand in for JUCE's default sans-serif and
    // monospaced fonts (UI chrome plan U2, KD1), so plain juce::FontOptions text
    // and stock JUCE components follow them. Bold is Barlow's SemiBold.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pb3LookAndFeel)
};

// The controls inside a dialog, as the mockup draws them (UI chrome U4):
// 28 px fields (combo boxes and text) with radius 5 and no border; 6 px-radius
// buttons, filled bgSecondary or ghost (the kOutlineButton property); toggles
// as fields, tinted teal when on; a value bar for horizontal sliders; and tabs
// as uppercase captions with a teal underline. DialogFrame
// installs it on itself, so the main window's controls don't change.
class Pb3DialogLookAndFeel : public Pb3LookAndFeel
{
public:
    Pb3DialogLookAndFeel();

    void drawButtonBackground(juce::Graphics&,
                              juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawToggleButton(juce::Graphics&,
                          juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&,
                      int width,
                      int height,
                      bool isButtonDown,
                      int buttonX,
                      int buttonY,
                      int buttonW,
                      int buttonH,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawLinearSlider(juce::Graphics&,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle,
                          juce::Slider&) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

    // Tables, lists and trees (UI chrome U7): uppercase caption headers over a
    // 1 px rule, rows on the dialog body, keys as small mono fields, and a
    // chevron for a tree's open and closed items.
    void drawTableHeaderBackground(juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn(juce::Graphics&,
                               juce::TableHeaderComponent&,
                               const juce::String& columnName,
                               int columnId,
                               int width,
                               int height,
                               bool isMouseOver,
                               bool isMouseDown,
                               int columnFlags) override;
    void drawKeymapChangeButton(
        juce::Graphics&, int width, int height, juce::Button&, const juce::String& keyDescription) override;
    void drawTreeviewPlusMinusBox(juce::Graphics&,
                                  const juce::Rectangle<float>& area,
                                  juce::Colour backgroundColour,
                                  bool isOpen,
                                  bool isMouseOver) override;
    bool areLinesDrawnForTreeView(juce::TreeView&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pb3DialogLookAndFeel)
};

} // namespace pedalboard3::ui
