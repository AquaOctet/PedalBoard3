#include "AppMappings.h"

#include "Patch.h" // appendMappingsXml / readMappingsXml

namespace pedalboard3::session
{

void AppMappings::setMidi(std::vector<control::MidiMapping> midi)
{
    if (midi == midi_)
        return;
    midi_ = std::move(midi);
    sendChangeMessage();
}

void AppMappings::setOsc(std::vector<control::OscMapping> osc)
{
    if (osc == osc_)
        return;
    osc_ = std::move(osc);
    sendChangeMessage();
}

std::unique_ptr<juce::XmlElement> AppMappings::toXml() const
{
    auto el = std::make_unique<juce::XmlElement>("APP_MAPPINGS");
    appendMappingsXml(*el, midi_, osc_);
    return el;
}

bool AppMappings::restoreFromXml(const juce::XmlElement& xml)
{
    if (!xml.hasTagName("APP_MAPPINGS"))
        return false;

    std::vector<control::MidiMapping> midi;
    std::vector<control::OscMapping> osc;
    readMappingsXml(xml, midi, osc);
    midi_ = std::move(midi);
    osc_ = std::move(osc);
    return true;
}

} // namespace pedalboard3::session
