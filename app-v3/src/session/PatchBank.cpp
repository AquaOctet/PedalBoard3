#include "PatchBank.h"

namespace pedalboard3::session
{

int PatchBank::add(Patch patch)
{
    patches.push_back(std::move(patch));
    return size() - 1;
}

int PatchBank::duplicate(int i)
{
    if (!isValidIndex(i))
        return -1;

    // Patch is a plain aggregate of vectors + juce::String/MemoryBlock, so a
    // copy is already deep — no shared state between original and copy.
    Patch copy = patches[static_cast<size_t>(i)];
    copy.name = copy.name + " copy";

    const int newIndex = i + 1;
    patches.insert(patches.begin() + newIndex, std::move(copy));
    if (activeIndex >= newIndex)
        ++activeIndex;
    return newIndex;
}

bool PatchBank::rename(int i, juce::String name)
{
    if (!isValidIndex(i))
        return false;
    name = name.trim();
    patches[static_cast<size_t>(i)].name = name.isEmpty() ? juce::String("Untitled") : name;
    return true;
}

bool PatchBank::remove(int i)
{
    if (!isValidIndex(i))
        return false;

    if (size() == 1)
    {
        patches = {Patch{}};
        activeIndex = 0;
        return true;
    }

    patches.erase(patches.begin() + i);
    if (activeIndex > i)
        --activeIndex;
    if (activeIndex >= size())
        activeIndex = size() - 1;
    return true;
}

bool PatchBank::select(int i) noexcept
{
    if (!isValidIndex(i))
        return false;
    activeIndex = i;
    return true;
}

void PatchBank::next() noexcept
{
    activeIndex = (activeIndex + 1) % size();
}

void PatchBank::previous() noexcept
{
    activeIndex = (activeIndex + size() - 1) % size();
}

std::unique_ptr<juce::XmlElement> bankToXml(const PatchBank& bank)
{
    auto el = std::make_unique<juce::XmlElement>("BANK");
    el->setAttribute("activeIndex", bank.isValidIndex(bank.activeIndex) ? bank.activeIndex : 0);
    for (const auto feature : kAllFeatures)
        if (!bank.featureOn(feature))
            el->setAttribute(featureKey(feature), "0");
    for (const auto& patch : bank.patches)
    {
        auto patchXml = patchToXml(patch);
        if (patchXml == nullptr)
            return nullptr;
        el->addChildElement(patchXml.release());
    }
    return el;
}

std::unique_ptr<PatchBank> bankFromXml(const juce::XmlElement& el)
{
    if (!el.hasTagName("BANK"))
        return nullptr;

    auto bank = std::make_unique<PatchBank>();
    bank->patches.clear();
    for (auto* patchEl : el.getChildWithTagNameIterator("PATCH"))
    {
        auto patch = patchFromXml(*patchEl);
        if (patch == nullptr || validatePatch(*patch) != PatchValidation::Ok)
            return nullptr;
        bank->patches.push_back(std::move(*patch));
    }
    if (bank->patches.empty())
        return nullptr;

    const int active = el.getIntAttribute("activeIndex", 0);
    bank->activeIndex = bank->isValidIndex(active) ? active : 0;
    for (const auto feature : kAllFeatures)
        bank->setFeature(feature, el.getBoolAttribute(featureKey(feature), true));
    return bank;
}

} // namespace pedalboard3::session
