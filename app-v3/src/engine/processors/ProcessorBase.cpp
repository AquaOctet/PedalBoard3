#include "ProcessorBase.h"

namespace pedalboard3::engine::processors
{

namespace
{
const juce::Identifier kStateTag{"PB3_PROCESSOR"};
constexpr int kStateVersion = 1;

juce::String parameterKey(const juce::AudioProcessorParameter& parameter)
{
    if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(&parameter))
        return "p_" + withId->paramID;
    return {};
}
} // namespace

ProcessorBase::ProcessorBase(const BusesProperties& buses, juce::String typeId)
    : juce::AudioProcessor(buses), typeId_(std::move(typeId))
{
}

ProcessorBase::~ProcessorBase()
{
    cancelPendingUpdate();
}

void ProcessorBase::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml(kStateTag);
    xml.setAttribute("type", typeId_);
    xml.setAttribute("version", kStateVersion);
    for (auto* parameter : getParameters())
        if (const auto key = parameterKey(*parameter); key.isNotEmpty())
            xml.setAttribute(key, static_cast<double>(parameter->getValue()));
    writeState(xml);
    copyXmlToBinary(xml, destData);
}

void ProcessorBase::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(kStateTag) || xml->getStringAttribute("type") != typeId_)
        return; // not this processor's state: keep the defaults

    // Restored while the node is built, before anything listens to it, so
    // plain setValue (no host notification) is right here.
    for (auto* parameter : getParameters())
        if (const auto key = parameterKey(*parameter); key.isNotEmpty() && xml->hasAttribute(key))
            parameter->setValue(static_cast<float>(xml->getDoubleAttribute(key)));
    readState(*xml);
}

void ProcessorBase::handOff(std::uint32_t bits) noexcept
{
    pending_.fetch_or(bits, std::memory_order_acq_rel);
    triggerAsyncUpdate(); // posts a pre-allocated message: no allocation, no lock
}

void ProcessorBase::handleAsyncUpdate()
{
    if (const auto bits = pending_.exchange(0, std::memory_order_acq_rel); bits != 0)
        messageThreadWork(bits);
}

} // namespace pedalboard3::engine::processors
