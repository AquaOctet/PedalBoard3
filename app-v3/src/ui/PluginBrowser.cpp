#include "PluginBrowser.h"
#include "engine/processors/LooperProcessor.h"
#include "engine/processors/MetronomeProcessor.h"
#include "session/FeatureGate.h"

#include "engine/PluginHost.h"
#include "Icons.h"
#include "session/BankDocument.h"
#include "session/EditHistory.h"
#include "session/Preferences.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kPad = 12;
constexpr int kHeaderHeight = 36;
constexpr int kSearchHeight = 30;
constexpr int kChipHeight = 22;
constexpr int kChipGap = 6;
constexpr int kFooterHeight = 44;
constexpr int kRowHeight = 34;

const juce::String kPluginPrefix{"plugin:"};
const juce::String kInternalPrefix{"internal:"};
const juce::String kDragPrefix{"pb3browser:"};
} // namespace

namespace
{
// A new Metronome or Looper starts from its Settings section (optional-features
// U2, KD7); other types start from their defaults.
juce::MemoryBlock settingsStateFor(AppContext& ctx, const juce::String& internalType)
{
    if (internalType == "MetronomeProcessor")
        return engine::processors::MetronomeProcessor::stateWithLevel(ctx.preferences.getNewMetronomeLevel());
    if (internalType == "LooperProcessor")
        if (const auto folder = ctx.preferences.getLooperFolder(); folder != juce::File())
            return engine::processors::LooperProcessor::stateForFolder(folder);
    return {};
}
} // namespace

bool builtInAvailable(AppContext& ctx, const juce::String& internalType)
{
    const auto feature = session::featureForNodeType(internalType);
    return !feature.has_value() || ctx.features.isAvailable(*feature);
}

std::vector<browser::Entry> browserEntries(AppContext& ctx)
{
    std::vector<browser::Entry> entries;
    for (const auto& type : ctx.pluginHost.getInternalNodeTypes())
        if (builtInAvailable(ctx, type.typeId))
            entries.push_back(
                {kInternalPrefix + type.typeId, type.displayName, {}, "Built-in", browser::kBuiltInChip, true});
    // Effects only (PluginHost::getKnownEffects).
    for (const auto& desc : ctx.pluginHost.getKnownEffects())
        entries.push_back({kPluginPrefix + desc.createIdentifierString(),
                           desc.name,
                           desc.manufacturerName,
                           desc.pluginFormatName,
                           browser::chipForCategory(desc.category),
                           false});
    return entries;
}

bool addBrowserEntry(AppContext& ctx,
                     const juce::String& id,
                     std::optional<juce::Point<float>> position,
                     const juce::MemoryBlock& state)
{
    // The same rule as the top bar's Add Plugin: only while Session is idle
    // and the chain is the live graph (Test Tone suspends live capture).
    if (ctx.session.getState() != session::Session::State::Idle || !ctx.bankDocument.isLiveCaptureEnabled())
        return false;

    if (id.startsWith(kInternalPrefix))
    {
        const auto type = id.fromFirstOccurrenceOf(kInternalPrefix, false, false);
        if (!builtInAvailable(ctx, type))
            return false; // switched off since the list or the drag began
        ctx.editHistory.addInternalNode(type, position, state.getSize() > 0 ? state : settingsStateFor(ctx, type));
    }
    else if (id.startsWith(kPluginPrefix))
    {
        const auto identifier = id.fromFirstOccurrenceOf(kPluginPrefix, false, false);
        const auto effects = ctx.pluginHost.getKnownEffects();
        const auto* desc =
            std::find_if(effects.begin(),
                         effects.end(),
                         [&identifier](const auto& d) { return d.createIdentifierString() == identifier; });
        if (desc == effects.end())
            return false;
        ctx.editHistory.addPlugin(*desc, position);
    }
    else
    {
        return false;
    }

    ctx.preferences.setRecentPlugins(browser::pushRecent(ctx.preferences.getRecentPlugins(), id));
    return true;
}

juce::String browserDragId(const juce::var& dragDescription)
{
    const auto text = dragDescription.toString();
    return text.startsWith(kDragPrefix) ? text.fromFirstOccurrenceOf(kDragPrefix, false, false) : juce::String();
}

// ---- PluginBrowser ----------------------------------------------------------------

PluginBrowser::PluginBrowser(AppContext& ctx) : ctx_(ctx)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    toggle_.setShape(panelToggleIcon(), false, true, false);
    toggle_.setTooltip("Collapse the plugin browser to its strip (Plugins > Plugin Browser does the same)");
    toggle_.onClick = [this]
    {
        if (onCollapse)
            onCollapse();
    };
    addAndMakeVisible(toggle_);

    search_.setTextToShowWhenEmpty("Search plugins", tokens::fgMuted);
    search_.onTextChange = [this] { refilter(); };
    search_.onEscapeKey = [this] { search_.clear(); };
    addAndMakeVisible(search_);

    list_.setModel(this);
    list_.setRowHeight(kRowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list_.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(list_);

    ctx_.pluginHost.getKnownPluginList().addChangeListener(this);
    ctx_.session.addChangeListener(this);
    ctx_.features.addChangeListener(this);
    rebuildEntries();
}

PluginBrowser::~PluginBrowser()
{
    ctx_.features.removeChangeListener(this);
    ctx_.session.removeChangeListener(this);
    ctx_.pluginHost.getKnownPluginList().removeChangeListener(this);
    list_.setModel(nullptr);
}

void PluginBrowser::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &ctx_.pluginHost.getKnownPluginList() || source == &ctx_.features)
        rebuildEntries(); // new plugins, or a built-in switched on or off
    else
        refilter(); // a Session change: an add may have updated the recents
}

void PluginBrowser::rebuildEntries()
{
    entries_ = browserEntries(ctx_);

    search_.setTextToShowWhenEmpty("Search " + juce::String(entries_.size()) + " plugins", tokens::fgMuted);
    rebuildChips();
    refilter();
}

void PluginBrowser::rebuildChips()
{
    const auto chips = browser::chipsFor(entries_);
    if (!chips.contains(chip_))
        chip_ = browser::kAllChip;

    chips_.clear();
    for (const auto& name : chips)
    {
        auto chip = std::make_unique<juce::TextButton>(name);
        chip->setClickingTogglesState(false);
        chip->setToggleState(name == chip_, juce::dontSendNotification);
        chip->setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
        chip->setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
        chip->onClick = [this, name]
        {
            chip_ = name;
            for (auto& c : chips_)
                c->setToggleState(c->getButtonText() == chip_, juce::dontSendNotification);
            refilter();
        };
        addAndMakeVisible(*chip);
        chips_.push_back(std::move(chip));
    }
    resized();
}

void PluginBrowser::refilter()
{
    rows_.clear();
    for (auto& group : browser::groupsFor(entries_, ctx_.preferences.getRecentPlugins(), search_.getText(), chip_))
    {
        rows_.push_back({true, group.title, {}});
        for (auto& entry : group.entries)
            rows_.push_back({false, {}, std::move(entry)});
    }
    list_.updateContent();
    list_.repaint();
}

int PluginBrowser::layoutChips(juce::Rectangle<int> area, bool apply)
{
    // Wrap the chips into rows; returns the height they take.
    int x = area.getX();
    int y = area.getY();
    const juce::Font font(juce::FontOptions(12.0f));
    for (auto& chip : chips_)
    {
        const int width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, chip->getButtonText())) + 18;
        if (x > area.getX() && x + width > area.getRight())
        {
            x = area.getX();
            y += kChipHeight + kChipGap;
        }
        if (apply)
            chip->setBounds(x, y, width, kChipHeight);
        x += width + kChipGap;
    }
    return chips_.empty() ? 0 : y + kChipHeight - area.getY();
}

void PluginBrowser::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
    g.drawHorizontalLine(getHeight() - kFooterHeight, 1.0f, static_cast<float>(getWidth()));

    const auto header = getLocalBounds().reduced(kPad, 0).removeFromTop(kHeaderHeight);
    g.setColour(tokens::fgMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    g.drawText("PLUGINS", header, juce::Justification::centredLeft);

    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawFittedText("Drag onto the canvas, or double-click to add at the end of the chain.",
                     getLocalBounds().removeFromBottom(kFooterHeight).reduced(kPad, 6),
                     juce::Justification::centredLeft,
                     2);
}

void PluginBrowser::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(kHeaderHeight).reduced(kPad, 6);
    toggle_.setBounds(header.removeFromRight(18).withSizeKeepingCentre(18, 18)); // the outer top corner
    area.removeFromBottom(kFooterHeight);

    auto body = area.reduced(kPad, 0);
    search_.setBounds(body.removeFromTop(kSearchHeight));
    body.removeFromTop(8);
    const int chipsHeight = layoutChips(body, true);
    body.removeFromTop(chipsHeight + 8);
    list_.setBounds(body.withTrimmedLeft(-6).withTrimmedRight(-6));
}

bool PluginBrowser::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onDismiss)
    {
        onDismiss();
        return true;
    }
    return false;
}

int PluginBrowser::getNumRows()
{
    return static_cast<int>(rows_.size());
}

void PluginBrowser::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(rows_.size()))
        return;
    const auto& row = rows_[static_cast<size_t>(rowNumber)];
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(6, 2);

    if (row.header)
    {
        g.setColour(tokens::fgMuted);
        g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        g.drawText(row.title.toUpperCase(), area.withTrimmedTop(8), juce::Justification::centredLeft);
        return;
    }

    if (rowIsSelected)
    {
        g.setColour(tokens::bgSecondary);
        g.fillRoundedRectangle(area.toFloat(), 6.0f);
    }

    const auto bar = area.removeFromLeft(12).withSizeKeepingCentre(5, 20).toFloat();
    g.setColour(row.entry.builtIn ? tokens::accentInfo : tokens::accentPrimary);
    g.fillRoundedRectangle(bar, 2.0f);

    g.setColour(tokens::fgMuted);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(row.entry.format, area.removeFromRight(52), juce::Justification::centredRight);

    g.setColour(tokens::fgPrimary);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(row.entry.name, area.withTrimmedLeft(4), juce::Justification::centredLeft, true);
}

void PluginBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(rows_.size()) || rows_[static_cast<size_t>(row)].header)
        return;
    if (addBrowserEntry(ctx_, rows_[static_cast<size_t>(row)].entry.id, std::nullopt) && onDismiss)
        onDismiss();
}

juce::var PluginBrowser::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
{
    if (rowsToDescribe.isEmpty())
        return {};
    const int row = rowsToDescribe[0];
    if (row < 0 || row >= static_cast<int>(rows_.size()) || rows_[static_cast<size_t>(row)].header)
        return {};
    return kDragPrefix + rows_[static_cast<size_t>(row)].entry.id;
}

// ---- BrowserRail ------------------------------------------------------------------

BrowserRail::BrowserRail()
{
    setOpaque(true);
    toggle_.setShape(panelToggleIcon(), false, true, false);
    toggle_.setTooltip("Open the plugin browser (Plugins > Plugin Browser does the same)");
    toggle_.onClick = [this]
    {
        if (onOpenPanel)
            onOpenPanel();
    };
    addAndMakeVisible(toggle_);
}

void BrowserRail::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
}

void BrowserRail::resized()
{
    toggle_.setBounds(getLocalBounds().reduced(6, 10).removeFromTop(36).withSizeKeepingCentre(18, 18));
}

} // namespace pedalboard3::ui
