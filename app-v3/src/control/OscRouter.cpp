#include "OscRouter.h"

namespace pedalboard3::control
{

void OscRouter::setMappings(std::vector<OscMapping> mappings)
{
    mappings_ = std::move(mappings);
    parsed_.clear();
    parsed_.reserve(mappings_.size());
    for (const auto& m : mappings_)
        parsed_.push_back(tryParseOscAddress(m.address));
}

std::vector<OscRouter::Update> OscRouter::process(const juce::OSCMessage& msg) const
{
    std::vector<Update> updates;
    const auto pattern = msg.getAddressPattern();

    for (size_t i = 0; i < mappings_.size(); ++i)
    {
        const auto& addr = parsed_[i];
        if (!addr.has_value() || !pattern.matches(*addr))
            continue;

        const auto& m = mappings_[i];
        const auto raw = oscArgumentValue(msg, m.argumentIndex);
        if (!raw.has_value())
            continue;

        updates.push_back({m.target, scaleToRange(*raw, m.rangeMin, m.rangeMax, m.inverted)});
    }

    return updates;
}

} // namespace pedalboard3::control
