#include "MappingsDialog.h"

#include "ColourScheme.h"
#include "session/ControlHub.h"
#include "session/Session.h"

#include <initializer_list>

namespace pedalboard3::ui
{

namespace
{

constexpr int kRowHeight = 26;
constexpr int kRowGap = 4;
constexpr int kSectionGap = 14;
constexpr int kCellGap = 4;
constexpr int kMinContentWidth = 720;

void configureRangeSlider(juce::Slider& s, float value, const juce::String& tooltip)
{
    s.setSliderStyle(juce::Slider::LinearBar);
    s.setRange(0.0, 1.0, 0.01);
    s.setNumDecimalPlacesToDisplay(2);
    s.setValue(value, juce::dontSendNotification);
    s.setTooltip(tooltip);
}

void configureIntSlider(juce::Slider& s, int minValue, int maxValue, int value, const juce::String& tooltip)
{
    s.setSliderStyle(juce::Slider::LinearBar);
    s.setRange(minValue, maxValue, 1.0);
    s.setNumDecimalPlacesToDisplay(0);
    s.setValue(value, juce::dontSendNotification);
    s.setTooltip(tooltip);
}

// Item id = parameter index + 1. A mapping whose index is past the plugin's
// parameter list (a different build of the plugin) keeps a placeholder item so
// the row still shows, and saves, what it targets.
void fillParameterBox(juce::ComboBox& box, const juce::StringArray& names, int selectedIndex)
{
    for (int i = 0; i < names.size(); ++i)
        box.addItem(names[i], i + 1);
    if (selectedIndex >= names.size())
        box.addItem("Parameter " + juce::String(selectedIndex + 1) + " (not in this plugin)", selectedIndex + 1);
    box.setSelectedId(selectedIndex + 1, juce::dontSendNotification);
    box.setTooltip("Target parameter");
}

void styleSectionHeader(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    label.setColour(juce::Label::textColourId, tokens::fgPrimary);
}

} // namespace

// ---------------------------------------------------------------- MIDI row

class MappingsPanel::MidiRow : public juce::Component
{
public:
    std::function<void(const control::MidiMapping&)> onChange;
    std::function<void()> onLearn;
    std::function<void()> onDelete;

    MidiRow(const control::MidiMapping& mapping, const juce::StringArray& parameterNames) : mapping_(mapping)
    {
        // Item ids follow control::MidiSourceKind's declaration order + 1.
        kind_.addItem("CC", 1);
        kind_.addItem("Note", 2);
        kind_.addItem("Program", 3);
        kind_.addItem("Pitch Bend", 4);
        kind_.setSelectedId(static_cast<int>(mapping_.source.kind) + 1, juce::dontSendNotification);
        kind_.setTooltip("Source: Control Change, Note, Program Change or Pitch Bend");

        channel_.addItem("Omni", 1);
        for (int ch = 1; ch <= 16; ++ch)
            channel_.addItem("Ch " + juce::String(ch), ch + 1);
        channel_.setSelectedId(mapping_.source.channel + 1, juce::dontSendNotification);
        channel_.setTooltip("MIDI channel this mapping listens on");

        configureIntSlider(number_, 0, 127, mapping_.source.controller, "CC or note number");
        fillParameterBox(param_, parameterNames, mapping_.target.parameterIndex);
        configureRangeSlider(min_, mapping_.rangeMin, "Range min: parameter value at the bottom of the control");
        configureRangeSlider(max_, mapping_.rangeMax, "Range max: parameter value at the top of the control");
        invert_.setToggleState(mapping_.inverted, juce::dontSendNotification);
        invert_.setTooltip("Invert the control");
        latch_.setToggleState(mapping_.latch, juce::dontSendNotification);
        latch_.setTooltip("Latch: each press toggles between min and max");
        learn_.setTooltip("Learn: move a control on your MIDI device");
        delete_.setTooltip("Delete this mapping");
        delete_.setColour(juce::TextButton::textColourOffId, tokens::accentDestructive);

        for (auto* c : std::initializer_list<juce::Component*>{
                 &kind_, &channel_, &number_, &param_, &min_, &max_, &invert_, &latch_, &learn_, &delete_})
            addAndMakeVisible(*c);

        kind_.onChange = [this] { pushChange(); };
        channel_.onChange = [this] { pushChange(); };
        param_.onChange = [this] { pushChange(); };
        number_.onValueChange = [this] { pushChange(); };
        min_.onValueChange = [this] { pushChange(); };
        max_.onValueChange = [this] { pushChange(); };
        invert_.onClick = [this] { pushChange(); };
        latch_.onClick = [this] { pushChange(); };
        learn_.onClick = [this]
        {
            if (onLearn)
                onLearn();
        };
        delete_.onClick = [this]
        {
            if (onDelete)
                onDelete();
        };

        updateNumberEnablement();
    }

    void setLearning(bool learning)
    {
        learn_.setButtonText(learning ? "..." : "Learn");
        learn_.setColour(juce::TextButton::textColourOffId, learning ? tokens::accentWarning : tokens::fgPrimary);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto take = [&r](juce::Component& c, int width)
        {
            c.setBounds(r.removeFromLeft(width));
            r.removeFromLeft(kCellGap);
        };
        take(kind_, 96);
        take(channel_, 70);
        take(number_, 56);
        delete_.setBounds(r.removeFromRight(28));
        r.removeFromRight(kCellGap);
        learn_.setBounds(r.removeFromRight(58));
        r.removeFromRight(kCellGap);
        latch_.setBounds(r.removeFromRight(62));
        r.removeFromRight(kCellGap);
        invert_.setBounds(r.removeFromRight(52));
        r.removeFromRight(kCellGap);
        max_.setBounds(r.removeFromRight(64));
        r.removeFromRight(kCellGap);
        min_.setBounds(r.removeFromRight(64));
        r.removeFromRight(kCellGap);
        param_.setBounds(r);
    }

private:
    void pushChange()
    {
        mapping_.source.kind = static_cast<control::MidiSourceKind>(juce::jmax(0, kind_.getSelectedId() - 1));
        mapping_.source.channel = juce::jmax(0, channel_.getSelectedId() - 1);
        mapping_.source.controller = static_cast<int>(number_.getValue());
        if (param_.getSelectedId() > 0)
            mapping_.target.parameterIndex = param_.getSelectedId() - 1;
        mapping_.rangeMin = static_cast<float>(min_.getValue());
        mapping_.rangeMax = static_cast<float>(max_.getValue());
        mapping_.inverted = invert_.getToggleState();
        mapping_.latch = latch_.getToggleState();
        updateNumberEnablement();
        if (onChange)
            onChange(mapping_);
    }

    // Program Change and Pitch Bend have no sub-address.
    void updateNumberEnablement()
    {
        const auto kind = mapping_.source.kind;
        number_.setEnabled(kind == control::MidiSourceKind::ControlChange || kind == control::MidiSourceKind::Note);
    }

    control::MidiMapping mapping_;
    juce::ComboBox kind_;
    juce::ComboBox channel_;
    juce::Slider number_;
    juce::ComboBox param_;
    juce::Slider min_;
    juce::Slider max_;
    juce::ToggleButton invert_{"Inv"};
    juce::ToggleButton latch_{"Latch"};
    juce::TextButton learn_{"Learn"};
    juce::TextButton delete_{"X"};
};

// ---------------------------------------------------------------- OSC row

class MappingsPanel::OscRow : public juce::Component
{
public:
    std::function<void(const control::OscMapping&)> onChange;
    std::function<void()> onDelete;

    OscRow(const control::OscMapping& mapping, const juce::StringArray& parameterNames) : mapping_(mapping)
    {
        address_.setText(mapping_.address, juce::dontSendNotification);
        address_.setTooltip("OSC address, e.g. /pb3/gain (press Enter to apply)");
        configureIntSlider(argument_, 0, 15, mapping_.argumentIndex, "Which argument of the message carries the value");
        fillParameterBox(param_, parameterNames, mapping_.target.parameterIndex);
        configureRangeSlider(min_, mapping_.rangeMin, "Range min");
        configureRangeSlider(max_, mapping_.rangeMax, "Range max");
        invert_.setToggleState(mapping_.inverted, juce::dontSendNotification);
        invert_.setTooltip("Invert the control");
        delete_.setTooltip("Delete this mapping");
        delete_.setColour(juce::TextButton::textColourOffId, tokens::accentDestructive);

        for (auto* c :
             std::initializer_list<juce::Component*>{&address_, &argument_, &param_, &min_, &max_, &invert_, &delete_})
            addAndMakeVisible(*c);

        address_.onReturnKey = [this] { pushChange(); };
        address_.onFocusLost = [this] { pushChange(); };
        argument_.onValueChange = [this] { pushChange(); };
        param_.onChange = [this] { pushChange(); };
        min_.onValueChange = [this] { pushChange(); };
        max_.onValueChange = [this] { pushChange(); };
        invert_.onClick = [this] { pushChange(); };
        delete_.onClick = [this]
        {
            if (onDelete)
                onDelete();
        };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        address_.setBounds(r.removeFromLeft(210));
        r.removeFromLeft(kCellGap);
        argument_.setBounds(r.removeFromLeft(56));
        r.removeFromLeft(kCellGap);
        delete_.setBounds(r.removeFromRight(28));
        r.removeFromRight(kCellGap);
        invert_.setBounds(r.removeFromRight(52));
        r.removeFromRight(kCellGap);
        max_.setBounds(r.removeFromRight(64));
        r.removeFromRight(kCellGap);
        min_.setBounds(r.removeFromRight(64));
        r.removeFromRight(kCellGap);
        param_.setBounds(r);
    }

private:
    void pushChange()
    {
        const auto address = address_.getText().trim();
        if (address == mapping_.address && static_cast<int>(argument_.getValue()) == mapping_.argumentIndex &&
            param_.getSelectedId() - 1 == mapping_.target.parameterIndex &&
            static_cast<float>(min_.getValue()) == mapping_.rangeMin &&
            static_cast<float>(max_.getValue()) == mapping_.rangeMax && invert_.getToggleState() == mapping_.inverted)
            return; // focus loss with nothing edited
        mapping_.address = address;
        mapping_.argumentIndex = static_cast<int>(argument_.getValue());
        if (param_.getSelectedId() > 0)
            mapping_.target.parameterIndex = param_.getSelectedId() - 1;
        mapping_.rangeMin = static_cast<float>(min_.getValue());
        mapping_.rangeMax = static_cast<float>(max_.getValue());
        mapping_.inverted = invert_.getToggleState();
        if (onChange)
            onChange(mapping_);
    }

    control::OscMapping mapping_;
    juce::TextEditor address_;
    juce::Slider argument_;
    juce::ComboBox param_;
    juce::Slider min_;
    juce::Slider max_;
    juce::ToggleButton invert_{"Inv"};
    juce::TextButton delete_{"X"};
};

// ---------------------------------------------------------------- panel

MappingsPanel::MappingsPanel(AppContext& ctx, session::NodeId node) : ctx_(ctx), node_(node)
{
    if (auto* proc = ctx_.session.getHostedPlugin(node_))
        for (auto* p : proc->getParameters())
            parameterNames_.add(p->getName(48));

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

    styleSectionHeader(midiHeader_, "MIDI");
    styleSectionHeader(oscHeader_, "OSC");
    rowsContent_.addAndMakeVisible(midiHeader_);
    rowsContent_.addAndMakeVisible(oscHeader_);
    rowsContent_.addAndMakeVisible(addMidiButton_);
    rowsContent_.addAndMakeVisible(addOscButton_);
    addMidiButton_.onClick = [this]
    {
        midi_.push_back(defaultMidiMapping());
        apply();
        rebuildRows();
    };
    addOscButton_.onClick = [this]
    {
        osc_.push_back(defaultOscMapping());
        apply();
        rebuildRows();
    };

    viewport_.setViewedComponent(&rowsContent_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    setSize(900, 460);
    rebuildRows();
}

MappingsPanel::~MappingsPanel()
{
    cancelLearn();
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
        auto row = std::make_unique<MidiRow>(midi_[i], parameterNames_);
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
        auto row = std::make_unique<OscRow>(osc_[i], parameterNames_);
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

control::MidiMapping MappingsPanel::defaultMidiMapping() const
{
    control::MidiMapping m;
    m.source.kind = control::MidiSourceKind::ControlChange;
    m.source.channel = 0;
    m.source.controller = 1;
    m.target.kind = control::MidiTargetKind::PluginParameter;
    m.target.node = node_;
    m.target.parameterIndex = 0;
    return m;
}

control::OscMapping MappingsPanel::defaultOscMapping() const
{
    control::OscMapping m;
    m.address = "/pb3/" + juce::String(node_) + "/" + juce::String(osc_.size() + 1);
    m.target.kind = control::MidiTargetKind::PluginParameter;
    m.target.node = node_;
    m.target.parameterIndex = 0;
    return m;
}

// ---------------------------------------------------------------- window

MappingsWindow::MappingsWindow(AppContext& ctx,
                               session::NodeId node,
                               const juce::String& nodeName,
                               std::function<void()> onClose)
    : juce::DocumentWindow("Mappings: " + nodeName, tokens::bgWindow, juce::DocumentWindow::closeButton),
      onClose_(std::move(onClose)), node_(node)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MappingsPanel(ctx, node), true);
    setResizable(true, false);
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
