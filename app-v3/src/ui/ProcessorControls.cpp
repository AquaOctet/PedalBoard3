#include "ProcessorControls.h"

#include "ColourScheme.h"
#include "session/Session.h"

#include <array>

namespace pedalboard3::ui
{

ProcessorControls::ProcessorControls(AppContext& ctx, session::NodeId node) : ctx_(ctx), node_(node)
{
    startTimerHz(15);
}

ProcessorControls::~ProcessorControls()
{
    stopTimer();
}

juce::AudioProcessor* ProcessorControls::processor() const
{
    if (ctx_.session.getState() != session::Session::State::Idle)
        return nullptr;
    return ctx_.session.getHostedPlugin(node_);
}

juce::AudioProcessorParameterWithID* ProcessorControls::parameter(const juce::String& id) const
{
    if (auto* p = processor())
        for (auto* candidate : p->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(candidate);
                withId != nullptr && withId->paramID == id)
                return withId;
    return nullptr;
}

namespace
{

// Output Toggle (M6 U10): one big 1 / 2 switch. The lit half is where the
// input goes; clicking the other half switches it.
class OutputToggleControls final : public ProcessorControls
{
public:
    OutputToggleControls(AppContext& ctx, session::NodeId node) : ProcessorControls(ctx, node)
    {
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            auto& button = buttons_[i];
            button.setButtonText(i == 0 ? "1" : "2");
            button.setClickingTogglesState(false);
            button.setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
            button.setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
            button.setColour(juce::TextButton::textColourOnId, tokens::fgInverse);
            button.setTooltip(i == 0 ? "Send the input to output 1 (the left output port)"
                                     : "Send the input to output 2 (the right output port)");
            button.setConnectedEdges(i == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
            button.onClick = [this, i] { choose(static_cast<int>(i)); };
            addAndMakeVisible(button);
        }
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        buttons_[0].setBounds(area.removeFromLeft(area.getWidth() / 2));
        buttons_[1].setBounds(area);
    }

private:
    juce::AudioParameterChoice* output() const
    {
        return dynamic_cast<juce::AudioParameterChoice*>(parameter("output"));
    }

    void choose(int index)
    {
        if (auto* p = output())
        {
            p->beginChangeGesture();
            *p = index; // notifies the host: a mapped footswitch and this stay in step
            p->endChangeGesture();
        }
        refresh();
    }

    void refresh() override
    {
        auto* p = output();
        const int chosen = p != nullptr ? p->getIndex() : -1;
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            buttons_[i].setEnabled(p != nullptr);
            buttons_[i].setToggleState(static_cast<int>(i) == chosen, juce::dontSendNotification);
        }
    }

    std::array<juce::TextButton, 2> buttons_;
};

} // namespace

std::unique_ptr<ProcessorControls> makeProcessorControls(AppContext& ctx,
                                                         const juce::String& internalType,
                                                         session::NodeId node)
{
    if (internalType == "OutputToggleProcessor")
        return std::make_unique<OutputToggleControls>(ctx, node);
    return nullptr;
}

} // namespace pedalboard3::ui
