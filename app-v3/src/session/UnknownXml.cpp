#include "UnknownXml.h"

namespace pedalboard3::session
{

UnknownXml UnknownXml::collect(const juce::XmlElement& el,
                               const juce::StringArray& knownAttributes,
                               const juce::StringArray& knownChildren)
{
    UnknownXml unknown;
    for (int i = 0; i < el.getNumAttributes(); ++i)
    {
        const auto& name = el.getAttributeName(i);
        if (!knownAttributes.contains(name))
            unknown.attributes.set(name, el.getAttributeValue(i));
    }
    for (auto* child : el.getChildIterator())
        if (!child->isTextElement() && !knownChildren.contains(child->getTagName()))
            unknown.keep(*child);
    return unknown;
}

void UnknownXml::keep(const juce::XmlElement& child)
{
    elements.add(child.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader()));
}

void UnknownXml::writeTo(juce::XmlElement& el) const
{
    const auto& names = attributes.getAllKeys();
    const auto& values = attributes.getAllValues();
    for (int i = 0; i < names.size(); ++i)
        if (!el.hasAttribute(names[i]))
            el.setAttribute(names[i], values[i]);

    // Each element is this version's own serialisation of one that was read
    // safely (no DTD, nothing outside the file), so it parses back as it was.
    for (const auto& text : elements)
        if (auto child = juce::parseXML(text))
            el.addChildElement(child.release());
}

} // namespace pedalboard3::session
