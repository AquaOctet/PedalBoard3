#include "TransportControls.h"

#include "Alert.h"

#include "engine/processors/MetronomeProcessor.h"
#include "engine/Transport.h"
#include "engine/TransportMath.h"
#include "Icons.h"
#include "session/FeatureGate.h"
#include "session/Preferences.h"
#include "session/Session.h"
#include "session/TransportCommands.h"
#include "TempoPopover.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kPad = 6;
constexpr int kIconButton = 26;
constexpr int kTempoWidth = 104;
constexpr int kPositionWidth = 52;
constexpr int kClickWidth = 28;
constexpr int kDividerGap = 9; // room for a divider between groups
constexpr int kControlHeight = 26;

juce::Font monoFont(float height)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::String signatureText(int numerator, int denominator)
{
    return juce::String(numerator) + "/" + juce::String(denominator);
}

const juce::String kDownArrow{juce::CharPointer_UTF8(" \xe2\x96\xbe")};  // ▾
const juce::String kQuarterNote{juce::CharPointer_UTF8("\xe2\x99\xa9")}; // ♩

void makeFlat(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    button.setColour(juce::TextButton::textColourOffId, tokens::fgPrimary);
    button.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
}
} // namespace

TransportControls::TransportControls(AppContext& ctx) : ctx_(ctx)
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

    makeFlat(tempoButton_);
    tempoButton_.setTooltip("This patch's tempo and time signature: click for Tap, -/+ and the time signature");
    tempoButton_.onClick = [this] { openTempoPopover(); };

    position_.setFont(monoFont(13.0f));
    position_.setJustificationType(juce::Justification::centred);
    position_.setColour(juce::Label::textColourId, tokens::fgMuted);
    position_.setTooltip("Position: bar.beat");

    makeFlat(clickButton_);
    clickButton_.setButtonText(kQuarterNote);
    clickButton_.setTooltip("The patch's Metronome: start or stop it (synced, the transport's Play runs it)");
    clickButton_.onClick = [this]
    {
        toggleMetronome();
        refresh();
    };

    for (auto* c : std::initializer_list<juce::Component*>{&playButton_, &returnButton_, &tempoButton_, &position_})
        addAndMakeVisible(c);
    addChildComponent(clickButton_);

    ctx_.session.addChangeListener(this);
    ctx_.features.addChangeListener(this);
    changeListenerCallback(nullptr); // find the patch's Metronome, then show it all
    startTimerHz(30);
}

TransportControls::~TransportControls()
{
    stopTimer();
    ctx_.features.removeChangeListener(this);
    ctx_.session.removeChangeListener(this);
}

void TransportControls::setFoldLevel(int level)
{
    level = juce::jlimit(0, 2, level);
    if (level == foldLevel_)
        return;
    foldLevel_ = level;
    position_.setVisible(foldLevel_ < 1);
    clickButton_.setVisible(clickShown());
    resized();
}

int TransportControls::preferredWidth() const noexcept
{
    return widthAtFold(foldLevel_);
}

int TransportControls::widthAtFold(int level) const noexcept
{
    // Priced, not applied: the bar decides which fold level fits before it
    // moves anything.
    int width = kPad + kIconButton + 2 + kIconButton + kDividerGap + kTempoWidth;
    if (level < 1)
        width += kDividerGap + kPositionWidth;
    if (wantClick_ && level < 2)
        width += kDividerGap + kClickWidth;
    return width + kPad;
}

void TransportControls::paint(juce::Graphics& g)
{
    const auto pill = getLocalBounds()
                          .toFloat()
                          .withSizeKeepingCentre(static_cast<float>(getWidth()), kControlHeight + 4.0f)
                          .reduced(0.5f);
    const float radius = pill.getHeight() * 0.5f;
    g.setColour(tokens::bgSecondary);
    g.fillRoundedRectangle(pill, radius);
    g.setColour(tokens::borderDefault);
    g.drawRoundedRectangle(pill, radius, 1.0f);
    for (const int x : dividers_)
        g.drawVerticalLine(x, pill.getY() + 6.0f, pill.getBottom() - 6.0f);
}

void TransportControls::resized()
{
    dividers_.clear();
    auto area = getLocalBounds().reduced(kPad, 0);
    const auto place = [&area](juce::Component& c, int width)
    { c.setBounds(area.removeFromLeft(width).withSizeKeepingCentre(width, kControlHeight)); };
    const auto divider = [this, &area]
    {
        dividers_.push_back(area.getX() + kDividerGap / 2);
        area.removeFromLeft(kDividerGap);
    };

    place(playButton_, kIconButton);
    area.removeFromLeft(2);
    place(returnButton_, kIconButton);
    divider();
    place(tempoButton_, kTempoWidth);
    if (foldLevel_ < 1)
    {
        divider();
        place(position_, kPositionWidth);
    }
    if (clickShown())
    {
        divider();
        place(clickButton_, kClickWidth);
    }
    repaint();
}

void TransportControls::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // The patch, or what's available, changed: find the patch's Metronome again.
    metronomeNode_.reset();
    for (const auto& node : ctx_.session.getActivePatch().nodes)
        if (node.internalType == "MetronomeProcessor")
        {
            metronomeNode_ = node.id;
            break;
        }
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

    const auto tempoText = juce::String(transport.getTempo(), 1) + " BPM" + kDownArrow;
    if (tempoButton_.getButtonText() != tempoText)
        tempoButton_.setButtonText(tempoText);

    const auto position = engine::transport::barsBeatsText(
        transport.getPpqPosition(), transport.getNumerator(), transport.getDenominator());
    if (position_.getText() != position)
        position_.setText(position, juce::dontSendNotification);
    position_.setColour(juce::Label::textColourId, playing ? tokens::fgPrimary : tokens::fgMuted);

    // ♩ follows the Metronome's switches, the patch and Settings > Metronome.
    const bool want = ctx_.features.isAvailable(session::Feature::Metronome) && metronomeNode_.has_value() &&
                      ctx_.preferences.getMetronomeClickButton();
    if (want != wantClick_)
    {
        wantClick_ = want;
        clickButton_.setVisible(clickShown());
        resized();
        if (onLayoutChanged)
            onLayoutChanged();
    }
    if (clickShown())
    {
        const auto* m = metronome();
        clickButton_.setColour(juce::TextButton::textColourOffId,
                               m != nullptr && m->isRunning() ? tokens::accentPrimary : tokens::fgMuted);
    }
}

engine::processors::MetronomeProcessor* TransportControls::metronome() const
{
    if (!metronomeNode_.has_value() || ctx_.session.getState() != session::Session::State::Idle)
        return nullptr; // a rebuild can free it
    return dynamic_cast<engine::processors::MetronomeProcessor*>(ctx_.session.getHostedPlugin(*metronomeNode_));
}

void TransportControls::toggleMetronome()
{
    if (auto* m = metronome())
    {
        if (m->isSynced())
            ctx_.transportCommands.playPause(); // synced, it runs with the transport
        else
            m->togglePlay();
    }
}

void TransportControls::openTempoPopover()
{
    auto popover = std::make_unique<TempoPopover>(ctx_);
    juce::Component::SafePointer<TransportControls> safeThis(this);
    popover->onOtherSignature = [safeThis]
    {
        if (auto* self = safeThis.getComponent())
            self->promptTimeSignature();
    };
    juce::CallOutBox::launchAsynchronously(std::move(popover), tempoButton_.getScreenBounds(), nullptr);
}

void TransportControls::promptTimeSignature()
{
    const auto& transport = ctx_.transportCommands.transport();
    juce::Component::SafePointer<TransportControls> safeThis(this);
    alert::show({"Time Signature",
                 "Beats per bar / note value, such as 5/8. Up to 16 beats of a 1, 2, 4, 8 or 16 note.",
                 {{"Set", 1, alert::Role::Main}, {"Cancel", 0, alert::Role::Cancel}},
                 {{"signature", signatureText(transport.getNumerator(), transport.getDenominator())}}},
                [safeThis](const alert::Response& response)
                {
                    auto* self = safeThis.getComponent();
                    if (self == nullptr || response.result != 1)
                        return;
                    const auto text = response.field("signature");
                    const int numerator = text.upToFirstOccurrenceOf("/", false, false).getIntValue();
                    const int denominator = text.fromFirstOccurrenceOf("/", false, false).getIntValue();
                    self->ctx_.session.setTimeSignature(numerator, denominator); // invalid: ignored
                    self->refresh();
                });
}

} // namespace pedalboard3::ui
