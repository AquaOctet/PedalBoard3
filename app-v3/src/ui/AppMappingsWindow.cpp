#include "AppMappingsWindow.h"

#include "ColourScheme.h"
#include "DialogStyle.h"
#include "MappingRowEditors.h"
#include "session/AppCommands.h"
#include "session/AppMappings.h"
#include "session/ControlHub.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <initializer_list>
#include <memory>
#include <vector>

namespace pedalboard3::ui
{

namespace
{

constexpr int kRowHeight = 26;
constexpr int kRowGap = 4;
constexpr int kSectionGap = 14;
constexpr int kCellGap = 4;
constexpr int kUnknownCommandItem = 1000;
constexpr int kSelectPatchItem = 1001;
constexpr int kPatchCell = 56;

// A mapping may name a patch the bank hasn't got yet — you lay out the
// footswitches before you build the set — and selecting a missing index is a
// no-op at the far end (session::PatchSwitcher). So the ceiling is a fixed,
// generous one rather than today's patch count.
constexpr int kMaxPatchNumber = 128;

// The target box: every app command, then Select Patch.
//
// Item id = index in session::kAppCommands + 1. An id that is not in this
// build's table keeps a placeholder so the row still shows, and saves, it.
//
// Select Patch is #97. control::MidiTargetKind::PatchSelect has been parsed,
// serialised and acted on since M3, but no editor could produce one, so the
// only way to get "this footswitch goes to patch 4" was to hand-edit a .pdl.
// (A Program Change already selects the patch of its own number by default;
// this is the case where any CC, note or Program Change picks a fixed one.)
void fillTargetBox(juce::ComboBox& box, const control::MidiTarget& target)
{
    int selected = 0;
    for (size_t i = 0; i < session::kAppCommands.size(); ++i)
    {
        box.addItem(session::kAppCommands[i].name, static_cast<int>(i) + 1);
        if (target.kind == control::MidiTargetKind::AppCommand && target.commandId == session::kAppCommands[i].id)
            selected = static_cast<int>(i) + 1;
    }
    box.addSeparator();
    box.addItem("Select Patch", kSelectPatchItem);
    if (target.kind == control::MidiTargetKind::PatchSelect)
        selected = kSelectPatchItem;

    if (selected == 0)
    {
        box.addItem("Unknown: " + target.commandId, kUnknownCommandItem);
        selected = kUnknownCommandItem;
    }
    box.setSelectedId(selected, juce::dontSendNotification);
    box.setTooltip("What this trigger does: run an app command, or switch to one patch");
}

bool boxSelectsPatch(const juce::ComboBox& box)
{
    return box.getSelectedId() == kSelectPatchItem;
}

// Read the target back out. `previous` carries the fields the box doesn't —
// an unknown command id we are preserving, and the patch index while an app
// command is selected — and `patchNumber` is the row's 1-based patch box.
control::MidiTarget targetFromBox(const juce::ComboBox& box, const control::MidiTarget& previous, int patchNumber)
{
    auto target = previous;
    if (boxSelectsPatch(box))
    {
        target.kind = control::MidiTargetKind::PatchSelect;
        target.patchIndex = juce::jmax(0, patchNumber - 1);
        return target;
    }

    target.kind = control::MidiTargetKind::AppCommand;
    const int id = box.getSelectedId();
    if (id >= 1 && id <= static_cast<int>(session::kAppCommands.size()))
        target.commandId = session::kAppCommands[static_cast<size_t>(id - 1)].id;
    return target;
}

// The patch-number box, which only shows while Select Patch is the target.
// It sits immediately right of the target box so the row reads "Select Patch
// | 4" in order, the way the sentence goes.
void configurePatchSlider(juce::Slider& s, const control::MidiTarget& target)
{
    configureIntSlider(s, 1, kMaxPatchNumber, target.patchIndex + 1, "Patch number to switch to");
}

// ------------------------------------------------------------ MIDI row

// Pitch Bend is not offered as a command trigger (plan KD4): every bend
// message would read as a press.
class AppMidiRow : public juce::Component
{
public:
    std::function<void(const control::MidiMapping&)> onChange;
    std::function<void()> onLearn;
    std::function<void()> onDelete;

    explicit AppMidiRow(const control::MidiMapping& mapping) : mapping_(mapping)
    {
        fillTargetBox(command_, mapping_.target);
        configurePatchSlider(patch_, mapping_.target);

        // Item ids follow control::MidiSourceKind's declaration order + 1.
        kind_.addItem("CC", 1);
        kind_.addItem("Note", 2);
        kind_.addItem("Program", 3);
        kind_.setSelectedId(static_cast<int>(mapping_.source.kind) + 1, juce::dontSendNotification);
        kind_.setTooltip("Trigger: a CC press (value 64 or more), a note-on, or any Program Change");

        channel_.addItem("Omni", 1);
        for (int ch = 1; ch <= 16; ++ch)
            channel_.addItem("Ch " + juce::String(ch), ch + 1);
        channel_.setSelectedId(mapping_.source.channel + 1, juce::dontSendNotification);
        channel_.setTooltip("MIDI channel");

        configureIntSlider(number_, 0, 127, mapping_.source.controller, "CC or note number");
        learn_.setTooltip("Learn: press the control on your MIDI device");
        delete_.setTooltip("Delete this mapping");
        delete_.setColour(juce::TextButton::textColourOffId, tokens::accentDestructive);

        for (auto* c :
             std::initializer_list<juce::Component*>{&command_, &kind_, &channel_, &number_, &learn_, &delete_})
            addAndMakeVisible(*c);
        addChildComponent(patch_); // shown only while Select Patch is the target

        command_.onChange = [this] { pushChange(); };
        kind_.onChange = [this] { pushChange(); };
        channel_.onChange = [this] { pushChange(); };
        number_.onValueChange = [this] { pushChange(); };
        patch_.onValueChange = [this] { pushChange(); };
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
        updateRowControls();
    }

    void setLearning(bool learning)
    {
        learn_.setButtonText(learning ? "..." : "Learn");
        learn_.setColour(juce::TextButton::textColourOffId, learning ? tokens::accentWarning : tokens::fgPrimary);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        command_.setBounds(r.removeFromLeft(180));
        if (patch_.isVisible())
        {
            r.removeFromLeft(kCellGap);
            patch_.setBounds(r.removeFromLeft(kPatchCell));
        }
        r.removeFromLeft(kCellGap);
        kind_.setBounds(r.removeFromLeft(96));
        r.removeFromLeft(kCellGap);
        channel_.setBounds(r.removeFromLeft(70));
        r.removeFromLeft(kCellGap);
        number_.setBounds(r.removeFromLeft(56));
        r.removeFromLeft(kCellGap);
        delete_.setBounds(r.removeFromRight(28));
        r.removeFromRight(kCellGap);
        learn_.setBounds(r.removeFromRight(58));
    }

private:
    void pushChange()
    {
        mapping_.target = targetFromBox(command_, mapping_.target, static_cast<int>(patch_.getValue()));
        mapping_.source.kind = static_cast<control::MidiSourceKind>(juce::jmax(0, kind_.getSelectedId() - 1));
        mapping_.source.channel = juce::jmax(0, channel_.getSelectedId() - 1);
        mapping_.source.controller = static_cast<int>(number_.getValue());
        updateRowControls();
        if (onChange)
            onChange(mapping_);
    }

    void updateRowControls()
    {
        number_.setEnabled(mapping_.source.kind != control::MidiSourceKind::ProgramChange);
        if (patch_.isVisible() != boxSelectsPatch(command_))
        {
            patch_.setVisible(boxSelectsPatch(command_));
            resized(); // the row is a column narrower or wider
        }
    }

    control::MidiMapping mapping_;
    juce::ComboBox command_;
    juce::Slider patch_;
    juce::ComboBox kind_;
    juce::ComboBox channel_;
    juce::Slider number_;
    juce::TextButton learn_{"Learn"};
    juce::TextButton delete_{"X"};
};

// ------------------------------------------------------------ OSC row

class AppOscRow : public juce::Component
{
public:
    std::function<void(const control::OscMapping&)> onChange;
    std::function<void()> onDelete;

    explicit AppOscRow(const control::OscMapping& mapping) : mapping_(mapping)
    {
        fillTargetBox(command_, mapping_.target);
        configurePatchSlider(patch_, mapping_.target);
        address_.setText(mapping_.address, juce::dontSendNotification);
        address_.setTooltip("OSC address; an argument of 0.5 or more runs the command (press Enter to apply)");
        configureIntSlider(argument_, 0, 15, mapping_.argumentIndex, "Which argument carries the button value");
        delete_.setTooltip("Delete this mapping");
        delete_.setColour(juce::TextButton::textColourOffId, tokens::accentDestructive);

        for (auto* c : std::initializer_list<juce::Component*>{&command_, &address_, &argument_, &delete_})
            addAndMakeVisible(*c);
        addChildComponent(patch_); // shown only while Select Patch is the target

        command_.onChange = [this] { pushChange(); };
        address_.onReturnKey = [this] { pushChange(); };
        address_.onFocusLost = [this] { pushChange(); };
        argument_.onValueChange = [this] { pushChange(); };
        patch_.onValueChange = [this] { pushChange(); };
        delete_.onClick = [this]
        {
            if (onDelete)
                onDelete();
        };
        updatePatchVisibility();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        command_.setBounds(r.removeFromLeft(180));
        if (patch_.isVisible())
        {
            r.removeFromLeft(kCellGap);
            patch_.setBounds(r.removeFromLeft(kPatchCell));
        }
        r.removeFromLeft(kCellGap);
        delete_.setBounds(r.removeFromRight(28));
        r.removeFromRight(kCellGap);
        argument_.setBounds(r.removeFromRight(56));
        r.removeFromRight(kCellGap);
        address_.setBounds(r);
    }

private:
    void pushChange()
    {
        const auto address = address_.getText().trim();
        const auto target = targetFromBox(command_, mapping_.target, static_cast<int>(patch_.getValue()));
        const int argument = static_cast<int>(argument_.getValue());
        updatePatchVisibility();
        if (address == mapping_.address && target == mapping_.target && argument == mapping_.argumentIndex)
            return; // focus loss with nothing edited
        mapping_.address = address;
        mapping_.target = target;
        mapping_.argumentIndex = argument;
        if (onChange)
            onChange(mapping_);
    }

    void updatePatchVisibility()
    {
        if (patch_.isVisible() == boxSelectsPatch(command_))
            return;
        patch_.setVisible(boxSelectsPatch(command_));
        resized(); // the address field gives up a column, or takes one back
    }

    control::OscMapping mapping_;
    juce::ComboBox command_;
    juce::Slider patch_;
    juce::TextEditor address_;
    juce::Slider argument_;
    juce::TextButton delete_{"X"};
};

// ------------------------------------------------------------ MIDI / OSC tab

class AppControlsPanel : public juce::Component
{
public:
    explicit AppControlsPanel(AppContext& ctx) : ctx_(ctx), midi_(ctx.appMappings.midi()), osc_(ctx.appMappings.osc())
    {
        hintLabel_.setText("These work in every patch: run an app command, or switch straight to one patch. "
                           "A button runs its command once per press.",
                           juce::dontSendNotification);
        hintLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);
        addAndMakeVisible(hintLabel_);

        dialogstyle::sectionHeader(midiHeader_, "MIDI");
        dialogstyle::sectionHeader(oscHeader_, "OSC");
        for (auto* c : std::initializer_list<juce::Component*>{&midiHeader_, &oscHeader_, &addMidi_, &addOsc_})
            content_.addAndMakeVisible(*c);

        addMidi_.onClick = [this]
        {
            midi_.push_back(defaultMidi());
            apply();
            rebuildRows();
        };
        addOsc_.onClick = [this]
        {
            osc_.push_back(defaultOsc());
            apply();
            rebuildRows();
        };

        viewport_.setViewedComponent(&content_, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);

        rebuildRows();
    }

    ~AppControlsPanel() override { cancelLearn(); }

    void paint(juce::Graphics& g) override { g.fillAll(tokens::bgWindow); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        hintLabel_.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        viewport_.setBounds(area);
        layoutRows();
    }

private:
    void layoutRows()
    {
        const int width = juce::jmax(520, viewport_.getMaximumVisibleWidth());
        int y = 0;
        auto section = [&](juce::Label& header, juce::TextButton& add)
        {
            header.setBounds(0, y, 120, kRowHeight);
            add.setBounds(width - 150, y, 150, kRowHeight);
            y += kRowHeight + kRowGap;
        };
        section(midiHeader_, addMidi_);
        for (auto& row : midiRows_)
        {
            row->setBounds(0, y, width, kRowHeight);
            y += kRowHeight + kRowGap;
        }
        y += kSectionGap;
        section(oscHeader_, addOsc_);
        for (auto& row : oscRows_)
        {
            row->setBounds(0, y, width, kRowHeight);
            y += kRowHeight + kRowGap;
        }
        content_.setSize(width, y);
    }

    void rebuildRows()
    {
        midiRows_.clear();
        oscRows_.clear();

        for (size_t i = 0; i < midi_.size(); ++i)
        {
            const int index = static_cast<int>(i);
            auto row = std::make_unique<AppMidiRow>(midi_[i]);
            row->onChange = [this, index](const control::MidiMapping& m)
            {
                midi_[static_cast<size_t>(index)] = m;
                apply();
            };
            row->onLearn = [this, index] { startLearn(index); };
            row->onDelete = [this, index] { deleteLater(true, index); };
            row->setLearning(index == learningRow_);
            content_.addAndMakeVisible(*row);
            midiRows_.push_back(std::move(row));
        }
        for (size_t i = 0; i < osc_.size(); ++i)
        {
            const int index = static_cast<int>(i);
            auto row = std::make_unique<AppOscRow>(osc_[i]);
            row->onChange = [this, index](const control::OscMapping& m)
            {
                osc_[static_cast<size_t>(index)] = m;
                apply();
            };
            row->onDelete = [this, index] { deleteLater(false, index); };
            content_.addAndMakeVisible(*row);
            oscRows_.push_back(std::move(row));
        }
        layoutRows();
    }

    void apply()
    {
        ctx_.appMappings.setMidi(midi_);
        ctx_.appMappings.setOsc(osc_);
    }

    void startLearn(int index)
    {
        if (learningRow_ == index)
        {
            cancelLearn();
            return;
        }
        cancelLearn();
        learningRow_ = index;
        if (index < static_cast<int>(midiRows_.size()))
            midiRows_[static_cast<size_t>(index)]->setLearning(true);

        juce::Component::SafePointer<AppControlsPanel> safeThis(this);
        ctx_.controlHub.armMidiLearn(
            [safeThis, index](const control::MidiSource& source)
            {
                auto* self = safeThis.getComponent();
                if (self == nullptr)
                    return;
                self->learningRow_ = -1;
                // A Pitch Bend is not a usable command trigger (KD4): keep the row.
                if (source.kind != control::MidiSourceKind::PitchBend && index < static_cast<int>(self->midi_.size()))
                {
                    self->midi_[static_cast<size_t>(index)].source = source;
                    self->apply();
                }
                self->rebuildRows();
            });
    }

    void cancelLearn()
    {
        if (learningRow_ < 0)
            return;
        ctx_.controlHub.cancelMidiLearn();
        if (learningRow_ < static_cast<int>(midiRows_.size()))
            midiRows_[static_cast<size_t>(learningRow_)]->setLearning(false);
        learningRow_ = -1;
    }

    // The clicked row is still inside its click; rebuild on the next turn.
    void deleteLater(bool midiRow, int index)
    {
        juce::Component::SafePointer<AppControlsPanel> safeThis(this);
        juce::MessageManager::callAsync(
            [safeThis, midiRow, index]
            {
                auto* self = safeThis.getComponent();
                if (self == nullptr)
                    return;
                self->cancelLearn();
                if (midiRow && index < static_cast<int>(self->midi_.size()))
                    self->midi_.erase(self->midi_.begin() + index);
                else if (!midiRow && index < static_cast<int>(self->osc_.size()))
                    self->osc_.erase(self->osc_.begin() + index);
                self->apply();
                self->rebuildRows();
            });
    }

    static control::MidiMapping defaultMidi()
    {
        control::MidiMapping m;
        m.source.kind = control::MidiSourceKind::ControlChange;
        m.source.channel = 0;
        m.source.controller = 64;
        m.target.kind = control::MidiTargetKind::AppCommand;
        m.target.commandId = session::appcommands::kNextPatch;
        return m;
    }

    control::OscMapping defaultOsc() const
    {
        control::OscMapping m;
        m.address = "/pb3/patch/next";
        if (!osc_.empty())
            m.address = "/pb3/patch/" + juce::String(osc_.size() + 1);
        m.target.kind = control::MidiTargetKind::AppCommand;
        m.target.commandId = session::appcommands::kNextPatch;
        return m;
    }

    AppContext& ctx_;
    std::vector<control::MidiMapping> midi_;
    std::vector<control::OscMapping> osc_;
    int learningRow_{-1};

    juce::Label hintLabel_;
    juce::Viewport viewport_;
    juce::Component content_;
    juce::Label midiHeader_;
    juce::Label oscHeader_;
    juce::TextButton addMidi_{"+ MIDI trigger"};
    juce::TextButton addOsc_{"+ OSC trigger"};
    std::vector<std::unique_ptr<AppMidiRow>> midiRows_;
    std::vector<std::unique_ptr<AppOscRow>> oscRows_;
};

} // namespace

std::unique_ptr<juce::Component> makeAppMappingsContent(AppContext& ctx,
                                                        juce::KeyPressMappingSet& keyMappings,
                                                        int initialTabIndex)
{
    auto tabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    auto keys = std::make_unique<juce::KeyMappingEditorComponent>(keyMappings, true);
    // It copies its background into its tree when it's built, before it's in
    // the dialog, so it's told the dialog's colours (UI chrome U7).
    keys->setColours(tokens::bgWindow, tokens::fgMenu);
    tabs->addTab("Keyboard", tokens::bgWindow, keys.release(), true);
    tabs->addTab("MIDI / OSC", tokens::bgWindow, new AppControlsPanel(ctx), true);
    tabs->setCurrentTabIndex(juce::jlimit(0, tabs->getNumTabs() - 1, initialTabIndex), juce::dontSendNotification);
    tabs->setSize(760, 460);
    return tabs;
}

AppMappingsWindow::AppMappingsWindow(AppContext& ctx,
                                     juce::KeyPressMappingSet& keyMappings,
                                     std::function<void()> onClose)
    : DialogFrame("App Mappings"), onClose_(std::move(onClose))
{
    setResizable(true, false);
    setContentOwned(makeAppMappingsContent(ctx, keyMappings).release(), true);
    setResizeLimits(560, 300, 1600, 1200);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void AppMappingsWindow::closeButtonPressed()
{
    if (onClose_)
        onClose_();
}

} // namespace pedalboard3::ui
