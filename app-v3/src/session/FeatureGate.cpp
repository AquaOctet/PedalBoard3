#include "FeatureGate.h"

#include "BankDocument.h"

namespace pedalboard3::session
{

FeatureGate::FeatureGate(GlobalStore store, BankDocument& board) : store_(std::move(store)), board_(board)
{
    for (const auto feature : kAllFeatures)
        global_[indexOf(feature)] = store_.load ? store_.load(feature) : enabledByDefault(feature);
    lastAvailable_ = availability();
    board_.addChangeListener(this);
}

FeatureGate::~FeatureGate()
{
    board_.removeChangeListener(this);
}

void FeatureGate::setEnabled(Feature feature, bool enabled)
{
    if (global_[indexOf(feature)] == enabled)
        return;
    global_[indexOf(feature)] = enabled;
    if (store_.save)
        store_.save(feature, enabled);
    lastAvailable_ = availability();
    sendChangeMessage(); // the Settings switch itself changed, even when the board has it off
}

bool FeatureGate::isOnForBoard(Feature feature) const noexcept
{
    return board_.bank().featureOn(feature);
}

void FeatureGate::setOnForBoard(Feature feature, bool on)
{
    board_.setBoardFeature(feature, on); // the board's change message re-evaluates availability
}

void FeatureGate::changeListenerCallback(juce::ChangeBroadcaster*)
{
    const auto now = availability();
    if (now == lastAvailable_)
        return;
    lastAvailable_ = now;
    sendChangeMessage();
}

std::array<bool, kFeatureCount> FeatureGate::availability() const noexcept
{
    std::array<bool, kFeatureCount> available{};
    for (const auto feature : kAllFeatures)
        available[indexOf(feature)] = isAvailable(feature);
    return available;
}

} // namespace pedalboard3::session
