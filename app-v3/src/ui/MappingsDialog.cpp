#include "MappingsDialog.h"

#include "ColourScheme.h"
#include "DialogStyle.h"
#include "session/ControlHub.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{

constexpr int kRowHeight = 26;
constexpr int kRowGap = 4;
constexpr int kSectionGap = 14;
constexpr int kMinContentWidth = 720;

} // namespace

// ---------------------------------------------------------------- panel

MappingsPanel::MappingsPanel(AppContext& ctx, session::NodeId node) : ctx_(ctx), node_(node)
{
    parameterNames_ = parameterNamesOf(ctx_.session, node_);

    const auto mappings = ctx_.session.getNodeMappings(node_);
    midi_ = mappings.midi;
    osc_ = mappings.osc;

    hintLabel_.setText("Changes apply immediately and are saved with the patch.", juce::dontSendNotification);
    hintLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    addAndMakeVisible(hintLabel_);

    channelLabel_.setText("MIDI input:", juce::dontSendNotification);
    channelLabel_.setJustificationType(juce::Justification::centredRight);
    channelLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    addAndMakeVisible(channelLabel_);

    channelBox_.addItem("All channels", 1);
    for (int ch = 1; ch <= 16; ++ch)
        channelBox_.addItem("Channel " + juce::String(ch), ch + 1);
    int channel = 0;
    for (const auto& s : ctx_.session.getNodeSnapshots())
        if (s.id == node_)
            channel = s.midiChannel;
    channelBox_.setSelectedId(channel + 1, juce::dontSendNotification);
    channelBox_.setTooltip("Only MIDI on this channel reaches the plugin");
    channelBox_.onChange = [this] { ctx_.session.setNodeMidiChannel(node_, channelBox_.getSelectedId() - 1); };
    addAndMakeVisible(channelBox_);

    footerLabel_.setText("Learn captures the next CC, note, program change or pitch bend. Range is the part of the "
                         "parameter the control sweeps.",
                         juce::dontSendNotification);
    footerLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
    footerLabel_.setMinimumHorizontalScale(0.8f);
    addAndMakeVisible(footerLabel_);

    dialogstyle::sectionHeader(midiHeader_, "MIDI");
    dialogstyle::sectionHeader(oscHeader_, "OSC");
    rowsContent_.addAndMakeVisible(midiHeader_);
    rowsContent_.addAndMakeVisible(oscHeader_);
    rowsContent_.addAndMakeVisible(addMidiButton_);
    rowsContent_.addAndMakeVisible(addOscButton_);
    addMidiButton_.onClick = [this]
    {
        midi_.push_back(defaultMidiMapping(node_));
        apply();
        rebuildRows();
    };
    addOscButton_.onClick = [this]
    {
        osc_.push_back(defaultOscMapping(node_, static_cast<int>(osc_.size()) + 1));
        apply();
        rebuildRows();
    };

    viewport_.setViewedComponent(&rowsContent_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    ctx_.session.addChangeListener(this);
    setSize(900, 460);
    rebuildRows();
}

MappingsPanel::~MappingsPanel()
{
    ctx_.session.removeChangeListener(this);
    cancelLearn();
}

// The board mappings panel edits the same mappings. When this node's mappings
// changed from there, show them; an edit made here comes back equal and
// changes nothing. An armed learn is left alone, since its row index must hold.
void MappingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (learningRow_ >= 0 || ctx_.session.getState() != session::Session::State::Idle || !ctx_.session.hasNode(node_))
        return;
    auto mappings = ctx_.session.getNodeMappings(node_);
    if (mappings.midi == midi_ && mappings.osc == osc_)
        return;
    midi_ = std::move(mappings.midi);
    osc_ = std::move(mappings.osc);
    rebuildRows();
}

void MappingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgWindow);
}

void MappingsPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto top = area.removeFromTop(28);
    channelBox_.setBounds(top.removeFromRight(160));
    channelLabel_.setBounds(top.removeFromRight(90));
    hintLabel_.setBounds(top);
    area.removeFromTop(8);
    footerLabel_.setBounds(area.removeFromBottom(22));
    area.removeFromBottom(6);
    viewport_.setBounds(area);
    layoutRows();
}

void MappingsPanel::layoutRows()
{
    const int width = juce::jmax(kMinContentWidth, viewport_.getMaximumVisibleWidth());
    int y = 0;
    auto section = [&](juce::Label& header, juce::TextButton& add)
    {
        header.setBounds(0, y, 120, kRowHeight);
        add.setBounds(width - 150, y, 150, kRowHeight);
        y += kRowHeight + kRowGap;
    };

    section(midiHeader_, addMidiButton_);
    for (auto& row : midiRows_)
    {
        row->setBounds(0, y, width, kRowHeight);
        y += kRowHeight + kRowGap;
    }
    y += kSectionGap;
    section(oscHeader_, addOscButton_);
    for (auto& row : oscRows_)
    {
        row->setBounds(0, y, width, kRowHeight);
        y += kRowHeight + kRowGap;
    }
    rowsContent_.setSize(width, y);
}

void MappingsPanel::rebuildRows()
{
    midiRows_.clear();
    oscRows_.clear();

    for (size_t i = 0; i < midi_.size(); ++i)
    {
        const int index = static_cast<int>(i);
        auto row = std::make_unique<MidiMappingRow>(midi_[i], parameterNames_);
        row->onChange = [this, index](const control::MidiMapping& m)
        {
            midi_[static_cast<size_t>(index)] = m;
            apply();
        };
        row->onLearn = [this, index] { startLearn(index); };
        row->onDelete = [this, index] { deleteMidiLater(index); };
        row->setLearning(index == learningRow_);
        rowsContent_.addAndMakeVisible(*row);
        midiRows_.push_back(std::move(row));
    }

    for (size_t i = 0; i < osc_.size(); ++i)
    {
        const int index = static_cast<int>(i);
        auto row = std::make_unique<OscMappingRow>(osc_[i], parameterNames_);
        row->onChange = [this, index](const control::OscMapping& m)
        {
            osc_[static_cast<size_t>(index)] = m;
            apply();
        };
        row->onDelete = [this, index] { deleteOscLater(index); };
        rowsContent_.addAndMakeVisible(*row);
        oscRows_.push_back(std::move(row));
    }

    layoutRows();
}

void MappingsPanel::apply()
{
    ctx_.session.setNodeMappings(node_, midi_, osc_); // false (no-op) once the node is gone
}

void MappingsPanel::startLearn(int midiIndex)
{
    if (learningRow_ == midiIndex)
    {
        cancelLearn(); // a second click on the armed row cancels
        return;
    }
    cancelLearn();

    learningRow_ = midiIndex;
    if (midiIndex < static_cast<int>(midiRows_.size()))
        midiRows_[static_cast<size_t>(midiIndex)]->setLearning(true);

    juce::Component::SafePointer<MappingsPanel> safeThis(this);
    ctx_.controlHub.armMidiLearn(
        [safeThis, midiIndex](const control::MidiSource& source)
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->learningRow_ = -1;
            if (midiIndex < static_cast<int>(self->midi_.size()))
            {
                self->midi_[static_cast<size_t>(midiIndex)].source = source;
                self->apply();
            }
            self->rebuildRows();
        });
}

void MappingsPanel::cancelLearn()
{
    if (learningRow_ < 0)
        return;
    ctx_.controlHub.cancelMidiLearn();
    if (learningRow_ < static_cast<int>(midiRows_.size()))
        midiRows_[static_cast<size_t>(learningRow_)]->setLearning(false);
    learningRow_ = -1;
}

// Deleting runs on the next message-loop turn: the row whose button was clicked
// is still inside that click when this is called, and rebuildRows destroys it.
void MappingsPanel::deleteMidiLater(int index)
{
    juce::Component::SafePointer<MappingsPanel> safeThis(this);
    juce::MessageManager::callAsync(
        [safeThis, index]
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr || index >= static_cast<int>(self->midi_.size()))
                return;
            self->cancelLearn(); // row indices shift below the deleted one
            self->midi_.erase(self->midi_.begin() + index);
            self->apply();
            self->rebuildRows();
        });
}

void MappingsPanel::deleteOscLater(int index)
{
    juce::Component::SafePointer<MappingsPanel> safeThis(this);
    juce::MessageManager::callAsync(
        [safeThis, index]
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr || index >= static_cast<int>(self->osc_.size()))
                return;
            self->osc_.erase(self->osc_.begin() + index);
            self->apply();
            self->rebuildRows();
        });
}

// ---------------------------------------------------------------- window

MappingsWindow::MappingsWindow(AppContext& ctx,
                               session::NodeId node,
                               const juce::String& nodeName,
                               std::function<void()> onClose)
    : DialogFrame("Mappings: " + nodeName), onClose_(std::move(onClose)), node_(node)
{
    setResizable(true, false);
    setContentOwned(new MappingsPanel(ctx, node), true);
    setResizeLimits(740, 260, 1800, 1400);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MappingsWindow::closeButtonPressed()
{
    if (onClose_)
        onClose_();
}

} // namespace pedalboard3::ui
