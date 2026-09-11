#include "PdlSerializer.h"

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

    for (const auto& patch : bank.patches)
    {
        auto patchXml = patchToXml(patch);
        if (patchXml == nullptr)
            return juce::Result::fail("Failed to serialise patch \"" + patch.name + "\"");
        bankEl->addChildElement(patchXml.release());
    }

    const auto parent = file.getParentDirectory();
    if (!parent.exists())
    {
        const auto created = parent.createDirectory();
        if (created.failed())
            return created;
    }

    // Atomic write: render to a temp file in the same directory, then move
    // it over the target so a crash mid-write can't truncate an existing
    // .pdl. (A full fsync/dir-fsync AtomicFileWriter per master plan §A.5
    // is a follow-up; juce::TemporaryFile gives us same-dir temp + rename.)
    juce::TemporaryFile temp(file);
    if (!root.writeTo(temp.getFile()))
        return juce::Result::fail("Could not write to " + file.getFullPathName());

    if (!temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Could not replace " + file.getFullPathName());

    return juce::Result::ok();
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

    if (!file.existsAsFile())
    {
        result.error = "File not found: " + file.getFullPathName();
        return result;
    }

    auto root = juce::parseXML(file);
    if (root == nullptr || !root->hasTagName("PEDALBOARD"))
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
        activeIndex = container->getIntAttribute("activeIndex", 0);
    else
        container = root.get();

    for (auto* patchEl : container->getChildWithTagNameIterator("PATCH"))
    {
        const int position = static_cast<int>(bank->patches.size()) + 1;

        auto patch = patchFromXml(*patchEl);
        if (patch == nullptr)
        {
            result.error = "Patch " + juce::String(position) + " is malformed";
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
