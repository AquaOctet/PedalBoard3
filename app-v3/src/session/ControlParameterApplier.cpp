#include "ControlParameterApplier.h"

namespace pedalboard3::session
{

namespace
{
thread_local bool applying = false;
} // namespace

bool ControlParameterApplier::isApplyingOnThisThread() noexcept
{
    return applying;
}

bool ControlParameterApplier::apply(const control::MidiTarget& target, float value) const
{
    if (target.kind != control::MidiTargetKind::PluginParameter)
        return false;
    if (!resolve_)
        return false;

    auto* proc = resolve_(target.node);
    if (proc == nullptr)
        return false;

    const auto& params = proc->getParameters();
    if (target.parameterIndex < 0 || target.parameterIndex >= params.size())
        return false;

    const juce::ScopedValueSetter<bool> scope(applying, true);
    params[target.parameterIndex]->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
    return true;
}

} // namespace pedalboard3::session
