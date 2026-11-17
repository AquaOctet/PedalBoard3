#include "PluginBrowser.h"
#include "engine/processors/LooperProcessor.h"
#include "engine/processors/MetronomeProcessor.h"
#include "Metrics.h"
#include "session/FeatureGate.h"

#include "engine/PluginHost.h"
#include "Fonts.h"
#include "Icons.h"
#include "session/BankDocument.h"
#include "session/EditHistory.h"
#include "session/Preferences.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kPad = metrics::kSpaceXl;
constexpr int kHeaderHeight = metrics::kPanelHeader;
constexpr int kSearchHeight = metrics::kSearchHeight;
constexpr int kFooterHeight = metrics::kFooterBrowser;
constexpr int kRowHeight = metrics::kRowList;

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
                     const juce::MemoryBlock& state,
                     std::optional<session::PatchConnection> insertInto)
{
    // The same rule as the top bar's Add Plugin: only while Session is idle
    // and the chain is the live graph.
    if (ctx.session.getState() != session::Session::State::Idle || !ctx.bankDocument.isLiveCaptureEnabled())
        return false;

    if (id.startsWith(kInternalPrefix))
    {
        const auto type = id.fromFirstOccurrenceOf(kInternalPrefix, false, false);
        if (!builtInAvailable(ctx, type))
            return false; // switched off since the list or the drag began
        ctx.editHistory.addInternalNode(
            type, position, state.getSize() > 0 ? state : settingsStateFor(ctx, type), insertInto);
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
        ctx.editHistory.addPlugin(*desc, position, insertInto);
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

    toggle_.setShape(panelSideIcon(PanelSide::Right), false, true, false);
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

    filter_.onChange = [this](juce::String chosen)
    {
        chip_ = std::move(chosen);
        refilter();
    };
    addAndMakeVisible(filter_);

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
    rebuildFilters();
    refilter();
}

void PluginBrowser::rebuildFilters()
{
    const auto chips = browser::chipsFor(entries_);
    chip_ = browser::keepOrResetChip(chips, chip_);
    filter_.setOptions(chips, chip_);
    resized();
}

void PluginBrowser::refilter()
{
    rows_.clear();
    for (auto& group : browser::groupsFor(entries_, ctx_.preferences.getRecentPlugins(), search_.getText(), chip_))
    {
        const auto shared = browser::sharedFormat(group);
        rows_.push_back({true, group.title, {}, shared});
        for (auto& entry : group.entries)
            rows_.push_back({false, {}, std::move(entry), shared});
    }
    list_.updateContent();
    list_.repaint();
}

void PluginBrowser::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
    g.drawHorizontalLine(getHeight() - kFooterHeight, 1.0f, static_cast<float>(getWidth()));

    // No PLUGINS caption. A panel of plugins with a "Search 12 plugins" box in
    // it does not need a label saying what it is, any more than the bank or
    // the mappings panel did (VR-U4).
    g.setColour(tokens::fgMuted);
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
    // The outer top corner, on the same vertical line as the top bar's
    // icon buttons (#120): the master mute sits directly above this one
    // and the two were nine pixels apart.
    toggle_.setBounds(
        header.removeFromRight(metrics::kIconButtonSize).withSizeKeepingCentre(metrics::kIconSize, metrics::kIconSize));
    area.removeFromBottom(kFooterHeight);

    auto body = area.reduced(kPad, 0);
    search_.setBounds(body.removeFromTop(kSearchHeight));
    body.removeFromTop(8);
    const int filterHeight = filter_.heightForWidth(body.getWidth());
    filter_.setBounds(body.removeFromTop(filterHeight));
    body.removeFromTop(8);
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
        // Sentence case at a normal weight, not bold capitals: three section
        // breaks in a list do not need to shout, they need to be findable
        // (VR-U4).
        auto header = area.withTrimmedTop(8);
        g.setColour(tokens::fgMuted);

        // The format every row below shares, said once here instead of on each
        // of them -- unless the section is already named after it, because
        // "Built-in / BUILT-IN" is the same decoration this unit is removing.
        if (row.sharedFormat.isNotEmpty() && !row.sharedFormat.equalsIgnoreCase(row.title))
        {
            g.setFont(fonts::ui(10.0f));
            g.drawText(row.sharedFormat.toUpperCase(), header.removeFromRight(52), juce::Justification::centredRight);
        }

        g.setFont(fonts::ui(11.0f, fonts::Weight::Medium));
        g.drawText(row.title, header, juce::Justification::centredLeft);
        return;
    }

    if (rowIsSelected)
    {
        g.setColour(tokens::bgSecondary);
        g.fillRoundedRectangle(area.toFloat(), 6.0f);
    }

    if (row.sharedFormat.isEmpty())
    {
        // A mixed section, so the format is the only thing telling a built-in
        // from a plugin of the same name.
        g.setColour(tokens::fgMuted);
        g.setFont(fonts::ui(10.0f));
        g.drawText(row.entry.format, area.removeFromRight(52), juce::Justification::centredRight);
    }

    g.setColour(tokens::fgPrimary);
    g.setFont(fonts::ui(14.0f));
    g.drawText(row.entry.name, area.withTrimmedLeft(metrics::kSpaceMd), juce::Justification::centredLeft, true);
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
    toggle_.setShape(panelSideIcon(PanelSide::Right), false, true, false);
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
