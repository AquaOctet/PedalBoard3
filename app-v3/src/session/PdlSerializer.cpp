#include "PdlSerializer.h"

#include "AtomicFileWriter.h"
#include "SafeXml.h"

namespace pedalboard3::session::pdl
{

juce::Result writeBank(const PatchBank& bank, const juce::File& file)
{
    if (bank.patches.empty())
        return juce::Result::fail("Cannot save an empty patch bank");

    // The one <BANK> writer, shared with Preferences and the unsaved-changes
    // signature, so the file and both of those always agree.
    auto bankEl = bankToXml(bank, /*withFileRoot*/ false);
    if (bankEl == nullptr)
        return juce::Result::fail("Failed to serialise the patch bank");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", kSchemaMajor);
    root.setAttribute("schemaMinor", kSchemaMinor);
    root.setAttribute("appVersion", PB3_APP_VERSION);
    root.addChildElement(bankEl.release());
    bank.fileUnknown.writeTo(root); // what a newer version wrote on the root (M5 U4)

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

namespace
{

BankLoadResult readBankFile(const juce::File& file)
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
    // A newer major version's board opens read-only instead of being refused
    // (M5 U4, plan KD7): this version reads what it can and never saves over it.
    result.readOnly = major > kSchemaMajor;

    // schemaMinor >= 4: <BANK> holding the patches. schemaMinor <= 3: a bare
    // <PATCH> under the root, read as a one-patch bank.
    auto bank = std::make_unique<PatchBank>();
    bank->patches.clear();
    bank->fileUnknown = UnknownXml::collect(*root, {"schemaMajor", "schemaMinor", "appVersion"}, {"BANK", "PATCH"});

    const juce::XmlElement* container = root->getChildByName("BANK");
    int activeIndex = 0;
    if (container != nullptr)
    {
        activeIndex = container->getIntAttribute("activeIndex", 0);
        readFeatureAttributes(*bank, *container); // the board's feature switches (optional-features U1)
        readBankUnknowns(*bank, *container);      // and what a newer version wrote there (M5 U4)
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

} // namespace

BankLoadResult readBank(const juce::File& file)
{
    auto result = readBankFile(file);
    if (result.bank == nullptr && result.readOnly)
        result.error =
            "It was saved by a newer version of Pedalboard 3, which this version can't read: " + result.error;
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
