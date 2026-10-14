#include "BoardMappingsPanel.h"

#include "Icons.h"
#include "MappingRowEditors.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
using Kind = mappingrows::Kind;

constexpr int kPad = 12;
constexpr int kColumnHeaderHeight = 22;
constexpr int kRowHeight = 28;
constexpr int kRowGap = 2;
constexpr int kPluginColumn = 150;
constexpr int kMinContentWidth = kPluginColumn + 740;
constexpr int kEmptyHeight = 48;

const juce::String kLearnText{juce::CharPointer_UTF8("Learn\xe2\x80\xa6")};
} // namespace

// ---- one row: the plugin's name, then the dialog's row editor ---------------

class BoardMappingsPanel::Row : public juce::Component
{
public:
    Row(const mappingrows::Row& model, std::unique_ptr<juce::Component> editor)
        : plugin_(model.plugin), selected_(model.selected), editor_(std::move(editor))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor); // the plugin name selects its card
        addAndMakeVisible(*editor_);
    }

    std::function<void()> onPluginClicked;

    bool isSelected() const noexcept { return selected_; }

    void setSelected(bool selected)
    {
        if (selected == selected_)
            return;
        selected_ = selected;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // The selected row follows the canvas selection, so it is marked with
        // the same "chosen" accent the selected card gets (#105).
        if (selected_)
        {
            g.setColour(tokens::accentSelected.withAlpha(0.10f));
            g.fillRect(getLocalBounds());
            g.setColour(tokens::accentSelected);
            g.fillRect(0, 0, 3, getHeight());
        }
        g.setColour(selected_ ? tokens::accentSelected : tokens::fgPrimary);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(plugin_, pluginArea(), juce::Justification::centredLeft, true);
    }

    void resized() override { editor_->setBounds(getLocalBounds().withTrimmedLeft(kPluginColumn).reduced(0, 1)); }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onPluginClicked && pluginArea().contains(e.getPosition()))
            onPluginClicked();
    }

private:
    juce::Rectangle<int> pluginArea() const { return getLocalBounds().removeFromLeft(kPluginColumn).reduced(10, 0); }

    juce::String plugin_;
    bool selected_;
    std::unique_ptr<juce::Component> editor_;
};

// ---- the column header above the rows, and the empty-board hint -------------

class BoardMappingsPanel::Content : public juce::Component
{
public:
    Kind kind{Kind::Midi};
    juce::String emptyText; // shown when the tab has no rows

    void paint(juce::Graphics& g) override
    {
        const int editorWidth = getWidth() - kPluginColumn;
        const auto spans =
            kind == Kind::Midi ? mappingcols::midiSpans(editorWidth) : mappingcols::oscSpans(editorWidth);

        g.setColour(tokens::fgMuted);
        g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        const auto label = [&g](const juce::String& text, int x, int width)
        { g.drawText(text, x, 0, width, kColumnHeaderHeight, juce::Justification::centredLeft, true); };
        label("PLUGIN", 10, kPluginColumn - 10);
        label("SOURCE", kPluginColumn + spans.source.getStart(), spans.source.getLength());
        label("CONTROLS", kPluginColumn + spans.control.getStart(), spans.control.getLength());
        label("RANGE", kPluginColumn + spans.range.getStart(), spans.range.getLength());
        label(kind == Kind::Midi ? "INV / LATCH" : "INV",
              kPluginColumn + spans.flags.getStart(),
              spans.flags.getLength());
        g.setColour(tokens::borderMuted);
        g.drawHorizontalLine(kColumnHeaderHeight - 1, 0.0f, static_cast<float>(getWidth()));

        if (emptyText.isNotEmpty())
        {
            g.setColour(tokens::fgMuted);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawFittedText(emptyText,
                             getLocalBounds().withTrimmedTop(kColumnHeaderHeight).reduced(10, 8),
                             juce::Justification::topLeft,
                             2);
        }
    }
};

// ---- the panel ----------------------------------------------------------------

BoardMappingsPanel::BoardMappingsPanel(AppContext& ctx) : ctx_(ctx), chips_(ctx), content_(std::make_unique<Content>())
{
    setOpaque(true);

    toggle_.setShape(panelToggleIcon(), false, true, false);
    toggle_.setTooltip("Collapse or open the mappings panel (Plugins > Mappings Panel does the same)");
    toggle_.onClick = [this]
    {
        if (onToggle)
            onToggle();
    };
    addAndMakeVisible(toggle_);

    for (auto* tab : {&midiTab_, &oscTab_})
    {
        tab->setClickingTogglesState(false);
        tab->setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
        tab->setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
        tab->setColour(juce::TextButton::textColourOnId, tokens::fgInverse);
        addAndMakeVisible(tab);
    }
    midiTab_.setTooltip("Every MIDI mapping on this board");
    oscTab_.setTooltip("Every OSC mapping on this board");
    midiTab_.onClick = [this] { showTab(Kind::Midi); };
    oscTab_.onClick = [this] { showTab(Kind::Osc); };

    addButton_.onClick = [this] { addMapping(); };
    addAndMakeVisible(addButton_);

    chips_.onLayoutChanged = [this] { resized(); };
    addAndMakeVisible(chips_);

    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(viewport_);

    ctx_.session.addChangeListener(this);
    refresh();
    rebuildRows(); // the empty hint and the column header, even before the first refresh lands
    updateHeader();
}

BoardMappingsPanel::~BoardMappingsPanel()
{
    ctx_.session.removeChangeListener(this);
    rows_.clear();
    viewport_.setViewedComponent(nullptr, false);
}

void BoardMappingsPanel::setOpen(bool open)
{
    open_ = open;
    viewport_.setVisible(open_);
    updateHeader();
    resized();
    repaint();
}

void BoardMappingsPanel::setSelectedNode(std::optional<session::NodeId> node)
{
    selected_ = node;
    for (auto* models : {&midi_, &osc_})
        for (auto& model : *models)
            model.selected = selected_ == model.node;

    const auto& models = modelsOf(tab_);
    for (size_t i = 0; i < rows_.size() && i < models.size(); ++i)
        rows_[i]->setSelected(models[i].selected);

    // Bring the first highlighted row into view.
    for (auto& row : rows_)
        if (row->isSelected())
        {
            if (!viewport_.getViewArea().contains(row->getBounds()))
                viewport_.setViewPosition(viewport_.getViewPositionX(),
                                          juce::jmax(0, row->getY() - kColumnHeaderHeight));
            break;
        }
    updateHeader();
}

void BoardMappingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void BoardMappingsPanel::refresh()
{
    // Parameter names come from the live processors, which are only safe to
    // read while Session is idle; reaching idle sends another change.
    if (ctx_.session.getState() != session::Session::State::Idle)
        return;

    const auto patch = ctx_.session.getActivePatch();

    // A different set of plugins drops the cached parameter names: a patch
    // swap can reuse node ids for other plugins.
    std::vector<juce::String> keys;
    for (const auto& node : patch.nodes)
        keys.push_back(
            juce::String(node.id) + ":" +
            (node.kind == session::NodeKind::Internal ? node.internalType : node.desc.createIdentifierString()));
    if (keys != nodeKeys_)
    {
        nodeKeys_ = std::move(keys);
        parameterNames_.clear();
    }

    nodeNames_.clear();
    for (const auto& s : ctx_.session.getNodeSnapshots())
        nodeNames_[s.id] = s.customName.isNotEmpty() ? s.customName : s.name;
    const auto nameOf = [this](const session::PatchNode& node)
    {
        const auto it = nodeNames_.find(node.id);
        return it != nodeNames_.end() ? it->second : mappingrows::defaultName(node);
    };

    auto midi = mappingrows::rowsFor(patch, Kind::Midi, selected_, nameOf);
    auto osc = mappingrows::rowsFor(patch, Kind::Osc, selected_, nameOf);
    const bool changed = !mappingrows::sameMappings(midi, midi_) || !mappingrows::sameMappings(osc, osc_);
    midi_ = std::move(midi);
    osc_ = std::move(osc);
    if (changed)
        rebuildRows();
    updateHeader();
}

void BoardMappingsPanel::rebuildRows()
{
    rows_.clear();
    const auto& models = modelsOf(tab_);
    for (size_t i = 0; i < models.size(); ++i)
    {
        const auto& model = models[i];
        const auto names = namesFor(model.node);
        std::unique_ptr<juce::Component> editor;
        if (model.kind == Kind::Midi)
        {
            auto midiRow = std::make_unique<MidiMappingRow>(model.midi, names);
            midiRow->setLearnLabel(kLearnText, "Opens this plugin's mappings dialog, where MIDI learn runs");
            midiRow->onChange = [this, i](const control::MidiMapping& m) { editMidi(i, m); };
            midiRow->onLearn = [this, node = model.node]
            {
                if (onOpenDialog)
                    onOpenDialog(node);
            };
            midiRow->onDelete = [this, i] { removeRow(Kind::Midi, i); };
            editor = std::move(midiRow);
        }
        else
        {
            auto oscRow = std::make_unique<OscMappingRow>(model.osc, names);
            oscRow->onChange = [this, i](const control::OscMapping& m) { editOsc(i, m); };
            oscRow->onDelete = [this, i] { removeRow(Kind::Osc, i); };
            editor = std::move(oscRow);
        }

        auto row = std::make_unique<Row>(model, std::move(editor));
        row->onPluginClicked = [this, node = model.node]
        {
            if (onSelectNode)
                onSelectNode(node);
        };
        content_->addAndMakeVisible(*row);
        rows_.push_back(std::move(row));
    }

    content_->kind = tab_;
    if (!rows_.empty())
        content_->emptyText = {};
    else if (tab_ == Kind::Midi)
        content_->emptyText = "No MIDI mappings on this board yet. Select a plugin and press + Mapping, or use M on "
                              "its card to learn one from your controller.";
    else
        content_->emptyText = "No OSC mappings on this board yet. Select a plugin and press + Mapping.";
    layoutRows();
    content_->repaint();
}

void BoardMappingsPanel::layoutRows()
{
    const int width = juce::jmax(kMinContentWidth, viewport_.getMaximumVisibleWidth());
    int y = kColumnHeaderHeight;
    for (auto& row : rows_)
    {
        row->setBounds(0, y, width, kRowHeight);
        y += kRowHeight + kRowGap;
    }
    content_->setSize(width, rows_.empty() ? y + kEmptyHeight : y);
}

void BoardMappingsPanel::showTab(Kind kind)
{
    if (!open_ && onToggle)
        onToggle(); // a tab on the collapsed header opens the panel on it
    if (kind == tab_ && !rows_.empty())
        return;
    tab_ = kind;
    rebuildRows();
    updateHeader();
}

void BoardMappingsPanel::updateHeader()
{
    midiTab_.setButtonText("MIDI " + juce::String(static_cast<int>(midi_.size())));
    oscTab_.setButtonText("OSC " + juce::String(static_cast<int>(osc_.size())));
    midiTab_.setToggleState(tab_ == Kind::Midi, juce::dontSendNotification);
    oscTab_.setToggleState(tab_ == Kind::Osc, juce::dontSendNotification);

    // + Mapping adds to the selected card, so it needs one with parameters.
    addButton_.setVisible(open_);
    if (!selected_.has_value())
    {
        addButton_.setEnabled(false);
        addButton_.setTooltip("Select a plugin on the canvas to add a mapping to it");
        return;
    }
    const auto named = nodeNames_.find(*selected_);
    const auto name = named != nodeNames_.end() ? named->second : juce::String("this plugin");
    if (namesFor(*selected_).isEmpty())
    {
        addButton_.setEnabled(false);
        addButton_.setTooltip(name + " has no parameters to map");
        return;
    }
    addButton_.setEnabled(true);
    addButton_.setTooltip("Add a " + juce::String(tab_ == Kind::Midi ? "MIDI" : "OSC") + " mapping to " + name);
}

// Edits write the node's whole tables back (KD6), the same call the dialog
// makes. The model row takes the new value first, so the Session change this
// causes reads as nothing new and leaves the editors, and the drag, alone.
void BoardMappingsPanel::editMidi(size_t row, const control::MidiMapping& mapping)
{
    if (row >= midi_.size())
        return;
    auto& model = midi_[row];
    auto mappings = ctx_.session.getNodeMappings(model.node);
    const auto at = static_cast<size_t>(model.nodeIndex);
    if (at >= mappings.midi.size())
        return;
    mappings.midi[at] = mapping;
    model.midi = mapping;
    ctx_.session.setNodeMappings(model.node, std::move(mappings.midi), std::move(mappings.osc));
}

void BoardMappingsPanel::editOsc(size_t row, const control::OscMapping& mapping)
{
    if (row >= osc_.size())
        return;
    auto& model = osc_[row];
    auto mappings = ctx_.session.getNodeMappings(model.node);
    const auto at = static_cast<size_t>(model.nodeIndex);
    if (at >= mappings.osc.size())
        return;
    mappings.osc[at] = mapping;
    model.osc = mapping;
    ctx_.session.setNodeMappings(model.node, std::move(mappings.midi), std::move(mappings.osc));
}

// The row goes when the Session change arrives, which is always after this
// click has returned (ChangeBroadcaster messages are asynchronous).
void BoardMappingsPanel::removeRow(Kind kind, size_t row)
{
    const auto& models = modelsOf(kind);
    if (row >= models.size())
        return;
    const auto node = models[row].node;
    const auto at = static_cast<size_t>(models[row].nodeIndex);
    auto mappings = ctx_.session.getNodeMappings(node);
    if (kind == Kind::Midi && at < mappings.midi.size())
        mappings.midi.erase(mappings.midi.begin() + static_cast<std::ptrdiff_t>(at));
    else if (kind == Kind::Osc && at < mappings.osc.size())
        mappings.osc.erase(mappings.osc.begin() + static_cast<std::ptrdiff_t>(at));
    else
        return;
    ctx_.session.setNodeMappings(node, std::move(mappings.midi), std::move(mappings.osc));
}

void BoardMappingsPanel::addMapping()
{
    if (!selected_.has_value())
        return;
    const auto node = *selected_;
    auto mappings = ctx_.session.getNodeMappings(node);
    if (tab_ == Kind::Midi)
        mappings.midi.push_back(defaultMidiMapping(node));
    else
        mappings.osc.push_back(defaultOscMapping(node, static_cast<int>(mappings.osc.size()) + 1));
    ctx_.session.setNodeMappings(node, std::move(mappings.midi), std::move(mappings.osc));
}

juce::StringArray BoardMappingsPanel::namesFor(session::NodeId node)
{
    if (const auto it = parameterNames_.find(node); it != parameterNames_.end())
        return it->second;
    // Only cache what was read from an idle graph.
    if (ctx_.session.getState() != session::Session::State::Idle)
        return {};
    auto names = parameterNamesOf(ctx_.session, node);
    parameterNames_[node] = names;
    return names;
}

void BoardMappingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    g.setColour(tokens::fgMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    g.drawText("MAPPINGS", titleArea_, juce::Justification::centredLeft);
}

void BoardMappingsPanel::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(kHeaderHeight).reduced(kPad, 5);
    toggle_.setBounds(header.removeFromLeft(18).withSizeKeepingCentre(18, 18)); // top-left, like the bank's
    header.removeFromLeft(10);
    titleArea_ = header.removeFromLeft(76);
    midiTab_.setBounds(header.removeFromLeft(70));
    header.removeFromLeft(4);
    oscTab_.setBounds(header.removeFromLeft(64));
    addButton_.setBounds(header.removeFromRight(96));
    // The board's feature switches: a footer's right end while open, the
    // header's while collapsed.
    constexpr int kFooterHeight = 26;
    const int chipsWidth = chips_.preferredWidth();
    if (open_ && chipsWidth > 0)
    {
        chips_.setBounds(area.removeFromBottom(kFooterHeight).reduced(kPad, 3).removeFromRight(chipsWidth));
    }
    else
    {
        header.removeFromRight(8);
        chips_.setBounds(header.removeFromRight(chipsWidth));
    }
    viewport_.setBounds(area);
    layoutRows();
}

} // namespace pedalboard3::ui
