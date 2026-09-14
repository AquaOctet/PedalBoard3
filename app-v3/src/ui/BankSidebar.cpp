#include "BankSidebar.h"

#include "Pb3LookAndFeel.h"
#include "session/FeatureGate.h"
#include "session/Preferences.h"

#include "ColourScheme.h"
#include "FileActions.h" // boardDisplayName
#include "Icons.h"
#include "session/BankDocument.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kPad = 12;
constexpr int kHeaderHeight = 36;
constexpr int kBoardNameHeight = 28;
constexpr int kFooterHeight = 48;
constexpr int kRowHeight = 40;
} // namespace

BankSidebar::BankSidebar(AppContext& ctx) : ctx_(ctx)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    list_.setModel(this);
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list_.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(list_);

    collapseButton_.setShape(panelToggleIcon(), false, true, false);
    collapseButton_.setTooltip("Collapse the patch bank to its strip (File > Patch Bank does the same)");
    collapseButton_.onClick = [this]
    {
        if (onCollapse)
            onCollapse();
    };
    newButton_.setTooltip("Add an empty patch and switch to it");
    newButton_.onClick = [this]
    {
        auto& doc = ctx_.bankDocument;
        session::Patch fresh;
        fresh.name = "Patch " + juce::String(doc.size() + 1);
        if (ctx_.features.isAvailable(session::Feature::Tempo))
            ctx_.preferences.applyNewPatchDefaults(fresh);
        doc.select(doc.addPatch(std::move(fresh)));
    };
    duplicateButton_.setTooltip("Copy the playing patch and switch to the copy");
    duplicateButton_.onClick = [this]
    {
        auto& doc = ctx_.bankDocument;
        doc.select(doc.duplicate(doc.activeIndex()));
    };
    for (auto* button : std::initializer_list<juce::Button*>{&collapseButton_, &newButton_, &duplicateButton_})
        addAndMakeVisible(button);

    ctx_.bankDocument.addChangeListener(this);
    ctx_.session.addChangeListener(this);
    refresh();
}

BankSidebar::~BankSidebar()
{
    ctx_.session.removeChangeListener(this);
    ctx_.bankDocument.removeChangeListener(this);
    list_.setModel(nullptr);
}

void BankSidebar::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void BankSidebar::refresh()
{
    const auto& doc = ctx_.bankDocument;
    // The playing patch's plugin count is live; while Test Tone has the graph
    // (or a swap is in flight) the slot is the truth instead.
    const int activeCount =
        doc.liveMatchesActiveSlot() ? ctx_.session.getNodeCount() : static_cast<int>(doc.activePatch().nodes.size());
    rows_ = bank::rowsFor(doc.bank(), activeCount);
    list_.updateContent();
    list_.repaint();
    repaint();
}

void BankSidebar::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    g.drawHorizontalLine(getHeight() - kFooterHeight, 0.0f, static_cast<float>(getWidth() - 1));

    auto area = getLocalBounds().reduced(kPad, 0);
    const auto header = area.removeFromTop(kHeaderHeight);
    g.setColour(tokens::fgMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    g.drawText("BANK", header.withTrimmedLeft(28), juce::Justification::centredLeft); // after the toggle

    const auto nameRow = area.removeFromTop(kBoardNameHeight);
    const int patches = ctx_.bankDocument.size();
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(
        juce::String(patches) + (patches == 1 ? " patch" : " patches"), nameRow, juce::Justification::centredRight);
    g.setColour(tokens::fgPrimary);
    g.setFont(juce::Font(juce::FontOptions(15.0f).withStyle("Bold")));
    g.drawText(boardDisplayName(ctx_.bankDocument.currentFile()),
               nameRow.withTrimmedRight(72),
               juce::Justification::centredLeft,
               true);
}

void BankSidebar::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(kHeaderHeight).reduced(kPad, 6);
    collapseButton_.setBounds(header.removeFromLeft(18).withSizeKeepingCentre(18, 18)); // under File
    area.removeFromTop(kBoardNameHeight);

    auto footer = area.removeFromBottom(kFooterHeight).reduced(kPad, 10);
    newButton_.setBounds(footer.removeFromLeft((footer.getWidth() - 8) / 2));
    footer.removeFromLeft(8);
    duplicateButton_.setBounds(footer);

    list_.setBounds(area.reduced(6, 0));
}

bool BankSidebar::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onDismiss)
    {
        onDismiss();
        return true;
    }
    return false;
}

int BankSidebar::getNumRows()
{
    return static_cast<int>(rows_.size());
}

void BankSidebar::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(rows_.size()))
        return;
    const auto& row = rows_[static_cast<size_t>(rowNumber)];

    const auto r = juce::Rectangle<int>(0, 0, width, height).reduced(2).toFloat();
    if (row.active)
    {
        g.setColour(tokens::accentPrimary.withAlpha(0.14f));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(tokens::accentPrimary);
        g.fillRect(r.withWidth(3.0f));
    }

    auto text = r.reduced(10.0f, 0.0f).toNearestInt();
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(row.active ? tokens::accentPrimary : tokens::fgMuted);
    g.drawText("PC " + juce::String(row.programChange), text.removeFromLeft(42), juce::Justification::centredLeft);
    g.setColour(tokens::fgMuted);
    g.drawText(juce::String(row.nodeCount), text.removeFromRight(28), juce::Justification::centredRight);

    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.setColour(row.active ? tokens::fgPrimary : tokens::fgPrimary.withAlpha(0.85f));
    g.drawText(row.name, text, juce::Justification::centredLeft, true);
}

void BankSidebar::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showRowMenu(row);
        return;
    }
    ctx_.bankDocument.select(row); // a no-op on the playing patch
    if (onDismiss)
        onDismiss();
}

void BankSidebar::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    ctx_.bankDocument.select(row);
    if (onRenameActive)
        onRenameActive();
}

juce::PopupMenu BankSidebar::makeRowMenu(int row)
{
    // Rename and Remove act on the playing patch (the top bar's prompts), so
    // they switch to the row first.
    juce::PopupMenu menu;
    menu.addItem("Rename...",
                 [this, row]
                 {
                     ctx_.bankDocument.select(row);
                     if (onRenameActive)
                         onRenameActive();
                 });
    menu.addItem("Duplicate",
                 [this, row]
                 {
                     auto& doc = ctx_.bankDocument;
                     doc.select(doc.duplicate(row));
                 });
    menu.addSeparator();
    menu.addItem("Remove...",
                 [this, row]
                 {
                     ctx_.bankDocument.select(row);
                     if (onRemoveActive)
                         onRemoveActive();
                 });
    return menu;
}

void BankSidebar::showRowMenu(int row)
{
    makeRowMenu(row).showMenuAsync(juce::PopupMenu::Options{}.withMinimumWidth(Pb3LookAndFeel::kMenuMinimumWidth));
}

} // namespace pedalboard3::ui
