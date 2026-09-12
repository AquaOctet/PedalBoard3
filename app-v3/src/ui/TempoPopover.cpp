#include "TempoPopover.h"

#include "ColourScheme.h"
#include "engine/Transport.h"
#include "engine/TransportMath.h"
#include "session/Session.h"
#include "session/TransportCommands.h"

#include <algorithm>
#include <cmath>

namespace pedalboard3::ui
{

namespace
{
constexpr int kPad = 12;
constexpr int kRow = 30;
constexpr int kTapHeight = 40;
constexpr int kChipHeight = 26;
constexpr int kChipsPerRow = 4;
constexpr int kGap = 4;
constexpr int kSignatures[][2] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}, {12, 8}};

juce::Font monoFont(float height)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::String signatureText(int numerator, int denominator)
{
    return juce::String(numerator) + "/" + juce::String(denominator);
}
} // namespace

// ---- the tempo: drag it, or double-click to type -----------------------------

class TempoPopover::TempoField : public juce::Label
{
public:
    std::function<void(double)> onTempoChange;

    TempoField()
    {
        setEditable(false, /*editOnDoubleClick*/ true, false);
        setJustificationType(juce::Justification::centred);
        setFont(monoFont(16.0f));
        setColour(juce::Label::textColourId, tokens::fgPrimary);
        setColour(juce::Label::backgroundColourId, tokens::bgSecondary);
        setColour(juce::Label::outlineColourId, tokens::borderDefault);
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setTooltip("Tempo (BPM): drag up or down, hold Shift for fine steps, or double-click to type");
        onTextChange = [this]
        {
            const double typed = getText().retainCharacters("0123456789.").getDoubleValue();
            if (typed > 0.0 && onTempoChange)
                onTempoChange(typed);
        };
    }

    void showTempo(double bpm)
    {
        tempo_ = bpm;
        if (!isBeingEdited() && !dragging_)
            setText(juce::String(bpm, 1), juce::dontSendNotification);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStartTempo_ = tempo_;
        dragging_ = false;
        juce::Label::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (isBeingEdited())
            return;
        const int dy = e.getDistanceFromDragStartY();
        if (!dragging_ && std::abs(dy) < 3)
            return;
        dragging_ = true;
        const double step = e.mods.isShiftDown() ? 0.1 : 0.5;
        const double next =
            engine::transport::clampTempo(std::round((dragStartTempo_ - dy * step) * 10.0) / 10.0); // up = faster
        setText(juce::String(next, 1), juce::dontSendNotification);
        if (onTempoChange)
            onTempoChange(next);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        dragging_ = false;
        juce::Label::mouseUp(e);
    }

private:
    double tempo_{engine::transport::kDefaultTempo};
    double dragStartTempo_{engine::transport::kDefaultTempo};
    bool dragging_{false};
};

// ---- the popover ------------------------------------------------------------------

TempoPopover::TempoPopover(AppContext& ctx) : ctx_(ctx), tempo_(std::make_unique<TempoField>())
{
    minus_.setTooltip("1 BPM slower");
    minus_.onClick = [this] { nudge(-1.0); };
    plus_.setTooltip("1 BPM faster");
    plus_.onClick = [this] { nudge(1.0); };
    tempo_->onTempoChange = [this](double bpm) { ctx_.session.setTempo(bpm); };
    bpmLabel_.setText("BPM", juce::dontSendNotification);
    bpmLabel_.setColour(juce::Label::textColourId, tokens::fgMuted);

    tap_.setTooltip("Tap in time: the tempo follows your last taps. A pause of more than 2 s starts again.");
    tap_.setColour(juce::TextButton::buttonColourId, tokens::accentPrimary);
    tap_.setColour(juce::TextButton::textColourOffId, tokens::fgInverse);
    tap_.onClick = [this]
    {
        ctx_.transportCommands.tap();
        refresh();
    };

    for (const auto& signature : kSignatures)
    {
        const int numerator = signature[0];
        const int denominator = signature[1];
        auto chip = std::make_unique<juce::TextButton>(signatureText(numerator, denominator));
        chip->setClickingTogglesState(false);
        chip->setColour(juce::TextButton::buttonColourId, tokens::bgMuted);
        chip->setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
        chip->setColour(juce::TextButton::textColourOnId, tokens::fgInverse);
        chip->onClick = [this, numerator, denominator]
        {
            ctx_.session.setTimeSignature(numerator, denominator);
            refresh();
        };
        addAndMakeVisible(*chip);
        signatures_.push_back(std::move(chip));
    }
    other_.setTooltip("Another time signature, such as 5/8");
    other_.setClickingTogglesState(false);
    other_.setColour(juce::TextButton::buttonColourId, tokens::bgMuted);
    other_.setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
    other_.setColour(juce::TextButton::textColourOnId, tokens::fgInverse);
    other_.onClick = [this]
    {
        const auto other = onOtherSignature; // the box goes once dismissed
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
        if (other)
            other();
    };

    note_.setText("Saved with this patch", juce::dontSendNotification);
    note_.setFont(juce::Font(juce::FontOptions(11.0f)));
    note_.setColour(juce::Label::textColourId, tokens::fgMuted);
    note_.setJustificationType(juce::Justification::centred);

    for (auto* c :
         std::initializer_list<juce::Component*>{&minus_, &plus_, tempo_.get(), &bpmLabel_, &tap_, &other_, &note_})
        addAndMakeVisible(c);

    setSize(292, 192);
    refresh();
    startTimerHz(10);
}

TempoPopover::~TempoPopover()
{
    stopTimer();
}

void TempoPopover::resized()
{
    auto area = getLocalBounds().reduced(kPad);

    auto row = area.removeFromTop(kRow);
    minus_.setBounds(row.removeFromLeft(kRow));
    row.removeFromLeft(6);
    auto units = row.removeFromRight(36);
    plus_.setBounds(row.removeFromRight(kRow));
    row.removeFromRight(6);
    tempo_->setBounds(row);
    bpmLabel_.setBounds(units.withTrimmedLeft(4));
    area.removeFromTop(8);

    tap_.setBounds(area.removeFromTop(kTapHeight));
    area.removeFromTop(8);

    // The time signatures, four to a row, "Other..." last.
    std::vector<juce::Component*> chips;
    for (auto& chip : signatures_)
        chips.push_back(chip.get());
    chips.push_back(&other_);
    const int chipWidth = (area.getWidth() - kGap * (kChipsPerRow - 1)) / kChipsPerRow;
    for (std::size_t i = 0; i < chips.size(); i += kChipsPerRow)
    {
        auto line = area.removeFromTop(kChipHeight);
        for (std::size_t j = i; j < std::min(i + kChipsPerRow, chips.size()); ++j)
        {
            chips[j]->setBounds(line.removeFromLeft(chipWidth));
            line.removeFromLeft(kGap);
        }
        area.removeFromTop(kGap);
    }
    area.removeFromTop(2);
    note_.setBounds(area.removeFromTop(18));
}

void TempoPopover::refresh()
{
    const auto& transport = ctx_.transportCommands.transport();
    tempo_->showTempo(transport.getTempo());
    const int numerator = transport.getNumerator();
    const int denominator = transport.getDenominator();
    bool common = false;
    for (std::size_t i = 0; i < signatures_.size(); ++i)
    {
        const bool current = kSignatures[i][0] == numerator && kSignatures[i][1] == denominator;
        common = common || current;
        signatures_[i]->setToggleState(current, juce::dontSendNotification);
    }
    other_.setToggleState(!common, juce::dontSendNotification);
    other_.setButtonText(common ? juce::String("Other...") : signatureText(numerator, denominator) + "...");
}

void TempoPopover::nudge(double delta)
{
    const double now = ctx_.transportCommands.transport().getTempo();
    ctx_.session.setTempo(engine::transport::clampTempo(std::round((now + delta) * 10.0) / 10.0));
    refresh();
}

} // namespace pedalboard3::ui
