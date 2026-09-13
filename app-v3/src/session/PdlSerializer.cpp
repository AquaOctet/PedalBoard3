#include "PdlSerializer.h"

#include "AtomicFileWriter.h"
#include "SafeXml.h"

namespace pedalboard3::session::pdl
{

juce::Result writeBank(const PatchBank& bank, const juce::File& file)
{
    if (bank.patches.empty())
        return juce::Result::fail("Cannot save an empty patch bank");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", kSchemaMajor);
    root.setAttribute("schemaMinor", kSchemaMinor);
    root.setAttribute("appVersion", PB3_APP_VERSION);

    auto* bankEl = root.createNewChildElement("BANK");
    bankEl->setAttribute("activeIndex", bank.isValidIndex(bank.activeIndex) ? bank.activeIndex : 0);
    writeFeatureAttributes(bank, *bankEl); // the board's feature switches (optional-features U1)

    for (const auto& patch : bank.patches)
    {
        auto patchXml = patchToXml(patch);
        if (patchXml == nullptr)
            return juce::Result::fail("Failed to serialise patch \"" + patch.name + "\"");
        bankEl->addChildElement(patchXml.release());
    }

    // Atomic and durable (M5 U2, AC-FS-6): a crash or power cut mid-save leaves
    // the old file or the new one, never a torn one.
    return AtomicFileWriter::write(file,
                                   [&root](juce::OutputStream& out)
                                   {
                                       root.writeTo(out);
                                       return true;
                                   });
}

juce::Result writePatch(const Patch& patch, const juce::File& file)
{
    PatchBank bank;
    bank.patches = {patch};
    bank.activeIndex = 0;
    return writeBank(bank, file);
}

BankLoadResult readBank(const juce::File& file)
{
    BankLoadResult result;

    // Read into memory, capped, with no DTDs or entities (M5 U1, AC-SE-7/8):
    // nothing outside the file can be read, and no file can exhaust memory.
    auto parsed = safexml::parseFile(file);
    if (parsed.xml == nullptr)
    {
        result.error = parsed.error;
        return result;
    }
    const auto& root = parsed.xml;
    if (!root->hasTagName("PEDALBOARD"))
    {
        result.error = "Not a Pedalboard 3 patch file";
        return result;
    }

    const int major = root->getIntAttribute("schemaMajor", -1);
    if (major < 0)
    {
        result.error = "Patch file is missing its schema version";
        return result;
    }
    if (major > kSchemaMajor)
    {
        result.error = "This patch was saved by a newer version of Pedalboard 3";
        return result;
    }

    // schemaMinor >= 4: <BANK> holding the patches. schemaMinor <= 3: a bare
    // <PATCH> under the root, read as a one-patch bank.
    auto bank = std::make_unique<PatchBank>();
    bank->patches.clear();

    const juce::XmlElement* container = root->getChildByName("BANK");
    int activeIndex = 0;
    if (container != nullptr)
    {
        activeIndex = container->getIntAttribute("activeIndex", 0);
        readFeatureAttributes(*bank, *container); // the board's feature switches (optional-features U1)
    }
    else
    {
        container = root.get();
    }

    int patchCount = 0;
    for ([[maybe_unused]] auto* patchEl : container->getChildWithTagNameIterator("PATCH"))
        ++patchCount;
    if (patchCount > kMaxPatchesPerBank)
    {
        result.error = "The file holds " + juce::String(patchCount) + " patches. Pedalboard 3 opens up to " +
                       juce::String(kMaxPatchesPerBank) + ".";
        return result;
    }

    for (auto* patchEl : container->getChildWithTagNameIterator("PATCH"))
    {
        const int position = static_cast<int>(bank->patches.size()) + 1;

        juce::String why;
        auto patch = patchFromXml(*patchEl, &why);
        if (patch == nullptr)
        {
            result.error = "Patch " + juce::String(position) + " is malformed" + (why.isNotEmpty() ? ": " + why : "");
            return result;
        }
        if (validatePatch(*patch) != PatchValidation::Ok)
        {
            result.error = "Patch " + juce::String(position) + " (\"" + patch->name +
                           "\") failed validation (corrupt or inconsistent)";
            return result;
        }
        bank->patches.push_back(std::move(*patch));
    }

    if (bank->patches.empty())
    {
        result.error = "Patch file has no <PATCH> element";
        return result;
    }

    bank->activeIndex = bank->isValidIndex(activeIndex) ? activeIndex : 0;
    result.bank = std::move(bank);
    return result;
}

LoadResult readPatch(const juce::File& file)
{
    LoadResult result;
    auto loaded = readBank(file);
    result.error = loaded.error;
    if (loaded.bank != nullptr)
        result.patch = std::make_unique<Patch>(std::move(loaded.bank->active()));
    return result;
}

} // namespace pedalboard3::session::pdl
