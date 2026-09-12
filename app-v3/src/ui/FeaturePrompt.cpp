#include "FeaturePrompt.h"

#include "session/BankDocument.h"
#include "session/FeatureGate.h"

namespace pedalboard3::ui
{

FeaturePrompt::FeaturePrompt(AppContext& ctx) : ctx_(ctx), seenLoads_(ctx.bankDocument.loadCount())
{
    ctx_.bankDocument.addChangeListener(this);
}

FeaturePrompt::~FeaturePrompt()
{
    ctx_.bankDocument.removeChangeListener(this);
}

void FeaturePrompt::changeListenerCallback(juce::ChangeBroadcaster*)
{
    const auto loads = ctx_.bankDocument.loadCount();
    if (loads == seenLoads_)
        return; // an edit, a patch switch: not a board opening
    seenLoads_ = loads;

    const auto features = session::featuresToAsk(ctx_.bankDocument.bank(),
                                                 [this](session::Feature f) { return ctx_.features.isEnabled(f); });
    if (features.empty())
        return;

    const bool several = features.size() > 1;
    const auto names = session::featureListText(features);
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle(several ? juce::String("Turn these features on?") : "Turn " + names + " on?")
                             .withMessage("This board uses " + names + ", which " + (several ? "are" : "is") +
                                          " off in Settings. Turn " + (several ? "them" : "it") + " on?")
                             .withButton("Turn On")
                             .withButton("Not Now");

    juce::WeakReference<FeaturePrompt> weak(this);
    juce::AlertWindow::showAsync(options,
                                 [weak, features](int result)
                                 {
                                     if (weak == nullptr || result != 1)
                                         return; // Not Now, or dismissed: nothing changes
                                     for (const auto feature : features)
                                         weak->ctx_.features.setEnabled(feature, true);
                                 });
}

} // namespace pedalboard3::ui
