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

namespace
{

// Carries the file root's unknowns inside <BANK> for the Preferences copy.
constexpr auto kFileRootTag = "PEDALBOARD";

juce::StringArray knownBankAttributes()
{
    juce::StringArray known{"activeIndex"};
    for (const auto feature : kAllFeatures)
        known.add(featureKey(feature));
    return known;
}

} // namespace

void readBankUnknowns(PatchBank& bank, const juce::XmlElement& bankElement)
{
    bank.unknown = UnknownXml::collect(bankElement, knownBankAttributes(), {"PATCH", kFileRootTag});
    if (auto* fileRoot = bankElement.getChildByName(kFileRootTag))
        bank.fileUnknown = UnknownXml::collect(*fileRoot, {}, {});
}

void writeFeatureAttributes(const PatchBank& bank, juce::XmlElement& bankElement)
{
    for (const auto feature : kAllFeatures)
        if (!bank.featureOn(feature))
            bankElement.setAttribute(featureKey(feature), "0");
}

void readFeatureAttributes(PatchBank& bank, const juce::XmlElement& bankElement)
{
    for (const auto feature : kAllFeatures)
        bank.setFeature(feature, bankElement.getBoolAttribute(featureKey(feature), true));
}

std::unique_ptr<juce::XmlElement> bankToXml(const PatchBank& bank, bool withFileRoot)
{
    auto el = std::make_unique<juce::XmlElement>("BANK");
    el->setAttribute("activeIndex", bank.isValidIndex(bank.activeIndex) ? bank.activeIndex : 0);
    writeFeatureAttributes(bank, *el);
    for (const auto& patch : bank.patches)
    {
        auto patchXml = patchToXml(patch);
        if (patchXml == nullptr)
            return nullptr;
        el->addChildElement(patchXml.release());
    }
    bank.unknown.writeTo(*el); // what a newer version wrote on the bank (M5 U4)
    if (withFileRoot && !bank.fileUnknown.isEmpty())
        bank.fileUnknown.writeTo(*el->createNewChildElement(kFileRootTag));
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
    readFeatureAttributes(*bank, el);
    readBankUnknowns(*bank, el);
    return bank;
}

} // namespace pedalboard3::session
