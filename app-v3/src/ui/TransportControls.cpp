#include "TransportControls.h"

#include "engine/Transport.h"
#include "engine/TransportMath.h"
#include "Icons.h"
#include "session/Session.h"
#include "session/TransportCommands.h"

#include <cmath>
#include <functional>

namespace pedalboard3::ui
{

namespace
{
constexpr int kIconButton = 28;
constexpr int kTempoWidth = 70;
constexpr int kTapWidth = 40;
constexpr int kSignatureWidth = 44;
constexpr int kPositionWidth = 70;
constexpr int kGap = 4;
constexpr int kGroupGap = 10;
constexpr int kControlHeight = 28;

juce::Font monoFont(float height)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::String signatureText(int numerator, int denominator)
{
    return juce::String(numerator) + "/" + juce::String(denominator);
}

// The common time signatures the menu offers; anything else is typed.
constexpr int kCommonSignatures[][2] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};
} // namespace

// ---- the tempo: drag it, or double-click to type -----------------------------

class TransportControls::TempoField : public juce::Label
{
public:
    std::function<void(double)> onTempoChange;

    TempoField()
    {
        setEditable(false, /*editOnDoubleClick*/ true, false);
        setJustificationType(juce::Justification::centred);
        setFont(monoFont(13.0f));
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

// ---- the transport ---------------------------------------------------------------

TransportControls::TransportControls(AppContext& ctx) : ctx_(ctx), tempo_(std::make_unique<TempoField>())
{
    playButton_.setShape(playIcon(), false, true, false);
    playButton_.setTooltip("Play (Space)");
    playButton_.onClick = [this]
    {
        ctx_.transportCommands.playPause();
        refresh();
    };

    returnButton_.setShape(returnToZeroIcon(), false, true, false);
    returnButton_.setTooltip("Return to Zero (Home)");
    returnButton_.onClick = [this]
    {
        ctx_.transportCommands.returnToZero();
        refresh();
    };

    tempo_->onTempoChange = [this](double bpm) { ctx_.session.setTempo(bpm); };

    tapButton_.setTooltip("Tap Tempo: tap in time; a pause of more than 2 s starts again");
    tapButton_.onClick = [this]
    {
        ctx_.transportCommands.tap();
        refresh();
    };

    timeSignatureButton_.setTooltip("Time signature of this patch");
    timeSignatureButton_.onClick = [this] { showTimeSignatureMenu(); };

    position_.setFont(monoFont(13.0f));
    position_.setJustificationType(juce::Justification::centred);
    position_.setColour(juce::Label::textColourId, tokens::fgMuted);
    position_.setTooltip("Position: bar.beat");

    for (auto* c : std::initializer_list<juce::Component*>{
             &playButton_, &returnButton_, tempo_.get(), &tapButton_, &timeSignatureButton_, &position_})
        addAndMakeVisible(c);

    refresh();
    startTimerHz(30);
}

TransportControls::~TransportControls()
{
    stopTimer();
}

void TransportControls::setFolded(bool folded)
{
    if (folded == folded_)
        return;
    folded_ = folded;
    timeSignatureButton_.setVisible(!folded_);
    position_.setVisible(!folded_);
    resized();
}

void TransportControls::resized()
{
    auto area = getLocalBounds();
    const auto place = [&area](juce::Component& c, int width, int gapAfter)
    {
        c.setBounds(area.removeFromLeft(width).withSizeKeepingCentre(width, kControlHeight));
        area.removeFromLeft(gapAfter);
    };
    place(playButton_, kIconButton, kGap);
    place(returnButton_, kIconButton, kGroupGap);
    place(*tempo_, kTempoWidth, kGap);
    place(tapButton_, kTapWidth, kGroupGap);
    if (!folded_)
    {
        place(timeSignatureButton_, kSignatureWidth, kGroupGap);
        place(position_, kPositionWidth, 0);
    }
}

void TransportControls::timerCallback()
{
    refresh();
}

void TransportControls::refresh()
{
    const auto& transport = ctx_.transportCommands.transport();
    const bool playing = transport.isPlaying();
    if (playing != shownPlaying_)
    {
        shownPlaying_ = playing;
        playButton_.setShape(playing ? pauseIcon() : playIcon(), false, true, false);
        playButton_.setColours(
            playing ? tokens::accentPrimary : tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary);
        playButton_.setTooltip(playing ? "Pause (Space)" : "Play (Space)");
    }

    tempo_->showTempo(transport.getTempo());

    const int numerator = transport.getNumerator();
    const int denominator = transport.getDenominator();
    const auto signature = signatureText(numerator, denominator);
    if (timeSignatureButton_.getButtonText() != signature)
        timeSignatureButton_.setButtonText(signature);

    const auto position = engine::transport::barsBeatsText(transport.getPpqPosition(), numerator, denominator);
    if (position_.getText() != position)
        position_.setText(position, juce::dontSendNotification);
    position_.setColour(juce::Label::textColourId, playing ? tokens::fgPrimary : tokens::fgMuted);
}

void TransportControls::showTimeSignatureMenu()
{
    const auto& transport = ctx_.transportCommands.transport();
    juce::PopupMenu menu;
    for (const auto& signature : kCommonSignatures)
    {
        const int numerator = signature[0];
        const int denominator = signature[1];
        const bool current = transport.getNumerator() == numerator && transport.getDenominator() == denominator;
        menu.addItem(signatureText(numerator, denominator),
                     true,
                     current,
                     [this, numerator, denominator]
                     {
                         ctx_.session.setTimeSignature(numerator, denominator);
                         refresh();
                     });
    }
    menu.addSeparator();
    menu.addItem("Other...", [this] { promptTimeSignature(); });
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(timeSignatureButton_));
}

void TransportControls::promptTimeSignature()
{
    const auto& transport = ctx_.transportCommands.transport();
    prompt_ = std::make_unique<juce::AlertWindow>(
        "Time Signature",
        "Beats per bar / note value, such as 5/8. Up to 16 beats of a 1, 2, 4, 8 or 16 note.",
        juce::MessageBoxIconType::NoIcon);
    prompt_->addTextEditor("signature", signatureText(transport.getNumerator(), transport.getDenominator()));
    prompt_->addButton("Set", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TransportControls> safeThis(this);
    prompt_->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis](int result)
            {
                auto* self = safeThis.getComponent();
                if (self == nullptr || self->prompt_ == nullptr)
                    return;
                if (result == 1)
                {
                    const auto text = self->prompt_->getTextEditorContents("signature");
                    const int numerator = text.upToFirstOccurrenceOf("/", false, false).getIntValue();
                    const int denominator = text.fromFirstOccurrenceOf("/", false, false).getIntValue();
                    self->ctx_.session.setTimeSignature(numerator, denominator); // invalid: ignored
                    self->refresh();
                }
                self->prompt_.reset();
            }),
        false);
}

} // namespace pedalboard3::ui
