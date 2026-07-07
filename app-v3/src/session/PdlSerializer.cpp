#include "PdlSerializer.h"

namespace pedalboard3::session::pdl
{

juce::Result writePatch(const Patch& patch, const juce::File& file)
{
    auto patchXml = patchToXml(patch);
    if (patchXml == nullptr)
        return juce::Result::fail("Failed to serialise patch");

    juce::XmlElement root("PEDALBOARD");
    root.setAttribute("schemaMajor", kSchemaMajor);
    root.setAttribute("schemaMinor", kSchemaMinor);
    root.setAttribute("appVersion", PB3_APP_VERSION);
    root.addChildElement(patchXml.release());

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

LoadResult readPatch(const juce::File& file)
{
    LoadResult result;

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

    auto* patchEl = root->getChildByName("PATCH");
    if (patchEl == nullptr)
    {
        result.error = "Patch file has no <PATCH> element";
        return result;
    }

    auto patch = patchFromXml(*patchEl);
    if (patch == nullptr)
    {
        result.error = "Patch file is malformed";
        return result;
    }

    if (validatePatch(*patch) != PatchValidation::Ok)
    {
        result.error = "Patch failed validation (corrupt or inconsistent)";
        return result;
    }

    result.patch = std::move(patch);
    return result;
}

} // namespace pedalboard3::session::pdl
