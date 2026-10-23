#include "StatusBar.h"

#include "ColourScheme.h"
#include "engine/AudioEngine.h"
#include "Icons.h"
#include "session/Session.h"

namespace pedalboard3::ui
{

namespace
{
constexpr int kBarPadding = 12;
constexpr int kDotSize = 8;
constexpr int kDotTextGap = 8;
constexpr int kToggleWidth = 28;
constexpr int kToggleGap = 2;
} // namespace

// One of the three panel toggles: an outline icon that fills with the accent
// while its panel is showing. A Button rather than a painted rectangle so it
// gets hover, keyboard focus and a tooltip for free.
class StatusBar::PanelToggle : public juce::Button
{
public:
    PanelToggle(const juce::String& name, PanelSide side) : juce::Button(name), side_(side) {}

    void setLit(bool lit)
    {
        if (lit == lit_)
            return;
        lit_ = lit;
        repaint();
    }

private:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto area = getLocalBounds().toFloat().reduced(2.0f, 4.0f);
        if (highlighted || down)
        {
            g.setColour(tokens::bgSecondary.withAlpha(down ? 0.9f : 0.55f));
            g.fillRoundedRectangle(area, 3.0f);
        }
        g.setColour(lit_ ? tokens::accentPrimary : tokens::fgSubtle);
        strokeIcon(g, panelSideIcon(side_), area.reduced(5.0f, 1.0f));
    }

    PanelSide side_;
    bool lit_{false};
};

StatusBar::StatusBar(AppContext& ctx) : ctx_(ctx), stateDotColour_(tokens::statusIdle)
{
    setOpaque(true);

    leftLabel_.setColour(juce::Label::textColourId, tokens::fgPrimary);
    leftLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    leftLabel_.setJustificationType(juce::Justification::centredLeft);
    leftLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(leftLabel_);

    errorLabel_.setColour(juce::Label::textColourId, tokens::statusError);
    errorLabel_.setColour(juce::Label::backgroundColourId, tokens::statusError.withAlpha(0.12f));
    errorLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    errorLabel_.setJustificationType(juce::Justification::centredRight);
    errorLabel_.setInterceptsMouseClicks(false, false);
    errorLabel_.setVisible(false);
    addAndMakeVisible(errorLabel_);

    patchesToggle_ = std::make_unique<PanelToggle>("Patches panel", PanelSide::Left);
    mappingsToggle_ = std::make_unique<PanelToggle>("Mappings panel", PanelSide::Bottom);
    browserToggle_ = std::make_unique<PanelToggle>("Plugin browser", PanelSide::Right);
    patchesToggle_->setTooltip("Show or hide the patch bank");
    mappingsToggle_->setTooltip("Show or hide the mappings panel");
    browserToggle_->setTooltip("Show or hide the plugin browser");
    patchesToggle_->onClick = [this]
    {
        if (onTogglePatchBank)
            onTogglePatchBank();
    };
    mappingsToggle_->onClick = [this]
    {
        if (onToggleMappingsPanel)
            onToggleMappingsPanel();
    };
    browserToggle_->onClick = [this]
    {
        if (onTogglePluginBrowser)
            onTogglePluginBrowser();
    };
    addAndMakeVisible(*patchesToggle_);
    addAndMakeVisible(*mappingsToggle_);
    addAndMakeVisible(*browserToggle_);

    ctx_.session.addChangeListener(this);
    ctx_.audioEngine.masterMuteChanged().addChangeListener(this);
    refresh();
}

StatusBar::~StatusBar()
{
    ctx_.audioEngine.masterMuteChanged().removeChangeListener(this);
    ctx_.session.removeChangeListener(this);
}

void StatusBar::paint(juce::Graphics& g)
{
    g.fillAll(tokens::bgSidebar);
    g.setColour(tokens::borderDefault);
    // One border, not two (#112). Where the collapsed mappings strip sits
    // directly above, its line is already the footer's top edge and a second
    // one here would cut the footer in half. The strip only spans the canvas
    // column, so the line is still drawn either side of it.
    if (topBorderGap_.isEmpty())
    {
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    }
    else
    {
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(topBorderGap_.getStart()));
        g.drawHorizontalLine(0, static_cast<float>(topBorderGap_.getEnd()), static_cast<float>(getWidth()));
    }

    // Status dot at the left edge — coloured per Session::State.
    const int dotX = kBarPadding;
    const int dotY = (getHeight() - kDotSize) / 2;
    g.setColour(stateDotColour_);
    g.fillEllipse(
        static_cast<float>(dotX), static_cast<float>(dotY), static_cast<float>(kDotSize), static_cast<float>(kDotSize));
}

void StatusBar::resized()
{
    auto bounds = getLocalBounds().reduced(kBarPadding, 0);

    // The toggles are centred on the window, not on what is left after the
    // chain text, so they stay put as that text changes length.
    const int groupWidth = kToggleWidth * 3 + kToggleGap * 2;
    auto group = getLocalBounds().withSizeKeepingCentre(groupWidth, getHeight());
    patchesToggle_->setBounds(group.removeFromLeft(kToggleWidth));
    group.removeFromLeft(kToggleGap);
    mappingsToggle_->setBounds(group.removeFromLeft(kToggleWidth));
    group.removeFromLeft(kToggleGap);
    browserToggle_->setBounds(group.removeFromLeft(kToggleWidth));

    auto text = bounds.reduced(0, 4);
    text.removeFromLeft(kDotSize + kDotTextGap); // space for the painted dot
    leftLabel_.setBounds(text.removeFromLeft(280));

    // The error pill takes the right-hand side, stopping clear of the toggles.
    const int rightEdge = group.getRight();
    auto right = text.withTrimmedLeft(juce::jmax(0, rightEdge + kBarPadding - text.getX()));
    errorLabel_.setBounds(right);
}

void StatusBar::refreshPanelToggles()
{
    patchesToggle_->setLit(isPatchBankShown && isPatchBankShown());
    mappingsToggle_->setLit(isMappingsPanelShown && isMappingsPanelShown());
    browserToggle_->setLit(isPluginBrowserShown && isPluginBrowserShown());
}

void StatusBar::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void StatusBar::refresh()
{
    const auto snaps = ctx_.session.getNodeSnapshots();
    const auto state = ctx_.session.getState();

    juce::String stateText;
    switch (state)
    {
    case session::Session::State::Building:
        stateText = "Loading...";
        stateDotColour_ = tokens::statusBuilding;
        break;
    case session::Session::State::Swapping:
        stateText = "Swapping...";
        stateDotColour_ = tokens::statusSwapping;
        break;
    case session::Session::State::Idle:
    default:
        stateText = "Idle";
        stateDotColour_ = tokens::statusIdle;
        break;
    }

    juce::String chainText = "Chain: " + juce::String(static_cast<int>(snaps.size())) + " plugin";
    if (snaps.size() != 1)
        chainText += "s";
    // Muted shows here as well as on the top bar's button, and it takes the
    // state dot with it (#106): the dot is the one thing on this bar that is
    // read at a glance, and a muted rig has to look muted from across a stage.
    const bool muted = ctx_.audioEngine.isMasterMuted();

    // The Test Tone (#111). It is a master stage now, so the board is still
    // there, still loaded and still editable — the tone is simply what is
    // reaching the output instead. That makes it exactly like MUTED: a
    // suffix on the normal line, not a replacement for it. It takes the dot
    // in amber, because "what you hear is not your board" is worth seeing
    // from across a stage, and mute wins the dot if both are on.
    const bool tone = isTestToneOn && isTestToneOn();
    if (tone)
        stateDotColour_ = tokens::accentWarning;
    if (muted)
        stateDotColour_ = tokens::accentDestructive;

    leftLabel_.setColour(juce::Label::textColourId,
                         muted  ? tokens::accentDestructive
                         : tone ? tokens::accentWarning
                                : tokens::fgPrimary);
    // Both suffixes, in that order, and neither hides the other. A muted rig
    // with the tone up is "I switched the tone on and got nothing" — the
    // exact report this came from — so MUTED has to stay visible alongside
    // it, and the chain count has to stay visible alongside both now that
    // the board really is still loaded.
    juce::String line = chainText + "  •  " + stateText;
    if (tone)
        line += "  •  TEST TONE";
    if (muted)
        line += "  •  MUTED";
    leftLabel_.setText(line, juce::dontSendNotification);

    // Error pill — shown when state is Idle and lastError is non-empty.
    // (During Building/Swapping the previous error from a prior run
    // stays hidden so the user isn't shown stale text mid-op.)
    const auto err = ctx_.session.getLastError();
    const bool show = state == session::Session::State::Idle && err.isNotEmpty();
    errorLabel_.setText(show ? "⚠  " + err : juce::String(), juce::dontSendNotification);
    errorLabel_.setVisible(show);

    repaint();
}

} // namespace pedalboard3::ui
