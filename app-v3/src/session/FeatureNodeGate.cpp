#include "FeatureNodeGate.h"

#include "engine/processors/ProcessorBase.h"
#include "FeatureGate.h"
#include "Session.h"

namespace pedalboard3::session
{

FeatureNodeGate::FeatureNodeGate(FeatureGate& features, Session& session) : features_(features), session_(session)
{
    features_.addChangeListener(this);
    session_.addChangeListener(this);
}

FeatureNodeGate::~FeatureNodeGate()
{
    session_.removeChangeListener(this);
    features_.removeChangeListener(this);
}

void FeatureNodeGate::apply()
{
    if (session_.getState() != Session::State::Idle)
        return; // a rebuild is under way; Session broadcasts again once it settles
    for (const auto& node : session_.getActivePatch().nodes)
        if (const auto feature = featureForNodeType(node.internalType))
            if (auto* processor = dynamic_cast<engine::processors::ProcessorBase*>(session_.getHostedPlugin(node.id)))
                processor->setFeatureOff(!features_.isAvailable(*feature));
}

} // namespace pedalboard3::session
