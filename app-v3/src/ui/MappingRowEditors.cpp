#include "MappingRowEditors.h"

#include "ColourScheme.h"
#include "session/Session.h"

#include <initializer_list>

namespace pedalboard3::ui
{

namespace
{

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

} // namespace

// ---------------------------------------------------------------- column spans

namespace mappingcols
{

// These follow MidiMappingRow::resized and OscMappingRow::resized exactly.
Spans midiSpans(int rowWidth)
{
    Spans s;
    const int sourceEnd = kKind + kGap + kChannel + kGap + kNumber;
    s.source = {0, sourceEnd};
    const int flagsEnd = rowWidth - kDelete - kGap - kLearn - kGap;
    s.flags = {flagsEnd - kLatch - kGap - kInvert, flagsEnd};
    const int rangeEnd = s.flags.getStart() - kGap;
    s.range = {rangeEnd - kMax - kGap - kMin, rangeEnd};
    s.control = {sourceEnd + kGap, juce::jmax(sourceEnd + kGap, s.range.getStart() - kGap)};
    return s;
}

Spans oscSpans(int rowWidth)
{
    Spans s;
    const int sourceEnd = kAddress + kGap + kArgument;
    s.source = {0, sourceEnd};
    const int flagsEnd = rowWidth - kDelete - kGap;
    s.flags = {flagsEnd - kInvert, flagsEnd};
    const int rangeEnd = s.flags.getStart() - kGap;
    s.range = {rangeEnd - kMax - kGap - kMin, rangeEnd};
    s.control = {sourceEnd + kGap, juce::jmax(sourceEnd + kGap, s.range.getStart() - kGap)};
    return s;
}

} // namespace mappingcols

using namespace mappingcols;

// ---------------------------------------------------------------- defaults

control::MidiMapping defaultMidiMapping(session::NodeId node)
{
    control::MidiMapping m;
    m.source.kind = control::MidiSourceKind::ControlChange;
    m.source.channel = 0;
    m.source.controller = 1;
    m.target.kind = control::MidiTargetKind::PluginParameter;
    m.target.node = node;
    m.target.parameterIndex = 0;
    return m;
}

control::OscMapping defaultOscMapping(session::NodeId node, int ordinal)
{
    control::OscMapping m;
    m.address = "/pb3/" + juce::String(node) + "/" + juce::String(ordinal);
    m.target.kind = control::MidiTargetKind::PluginParameter;
    m.target.node = node;
    m.target.parameterIndex = 0;
    return m;
}

juce::StringArray parameterNamesOf(session::Session& activeSession, session::NodeId node)
{
    juce::StringArray names;
    if (auto* proc = activeSession.getHostedPlugin(node))
        for (auto* p : proc->getParameters())
            names.add(p->getName(48));
    return names;
}

// ---------------------------------------------------------------- MIDI row

MidiMappingRow::MidiMappingRow(const control::MidiMapping& mapping, const juce::StringArray& parameterNames)
    : mapping_(mapping)
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

void MidiMappingRow::setLearning(bool learning)
{
    learn_.setButtonText(learning ? juce::String("...") : learnText_);
    learn_.setColour(juce::TextButton::textColourOffId, learning ? tokens::accentWarning : tokens::fgPrimary);
}

void MidiMappingRow::setLearnLabel(const juce::String& text, const juce::String& tooltip)
{
    learnText_ = text;
    learn_.setButtonText(text);
    learn_.setTooltip(tooltip);
}

void MidiMappingRow::resized()
{
    auto r = getLocalBounds();
    auto take = [&r](juce::Component& c, int width)
    {
        c.setBounds(r.removeFromLeft(width));
        r.removeFromLeft(kGap);
    };
    take(kind_, kKind);
    take(channel_, kChannel);
    take(number_, kNumber);
    delete_.setBounds(r.removeFromRight(kDelete));
    r.removeFromRight(kGap);
    learn_.setBounds(r.removeFromRight(kLearn));
    r.removeFromRight(kGap);
    latch_.setBounds(r.removeFromRight(kLatch));
    r.removeFromRight(kGap);
    invert_.setBounds(r.removeFromRight(kInvert));
    r.removeFromRight(kGap);
    max_.setBounds(r.removeFromRight(kMax));
    r.removeFromRight(kGap);
    min_.setBounds(r.removeFromRight(kMin));
    r.removeFromRight(kGap);
    param_.setBounds(r);
}

void MidiMappingRow::pushChange()
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

void MidiMappingRow::updateNumberEnablement()
{
    const auto kind = mapping_.source.kind;
    number_.setEnabled(kind == control::MidiSourceKind::ControlChange || kind == control::MidiSourceKind::Note);
}

// ---------------------------------------------------------------- OSC row

OscMappingRow::OscMappingRow(const control::OscMapping& mapping, const juce::StringArray& parameterNames)
    : mapping_(mapping)
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

void OscMappingRow::resized()
{
    auto r = getLocalBounds();
    address_.setBounds(r.removeFromLeft(kAddress));
    r.removeFromLeft(kGap);
    argument_.setBounds(r.removeFromLeft(kArgument));
    r.removeFromLeft(kGap);
    delete_.setBounds(r.removeFromRight(kDelete));
    r.removeFromRight(kGap);
    invert_.setBounds(r.removeFromRight(kInvert));
    r.removeFromRight(kGap);
    max_.setBounds(r.removeFromRight(kMax));
    r.removeFromRight(kGap);
    min_.setBounds(r.removeFromRight(kMin));
    r.removeFromRight(kGap);
    param_.setBounds(r);
}

void OscMappingRow::pushChange()
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

} // namespace pedalboard3::ui
