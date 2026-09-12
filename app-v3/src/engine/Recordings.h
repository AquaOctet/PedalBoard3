#pragma once

#include <juce_core/juce_core.h>

namespace pedalboard3::engine::recordings
{

// Where recordings go and what they're called (M6 U7, plan KD6): every take is
// a new WAV named after the patch and the moment it started, and nothing is
// ever overwritten. Pure, so the headless tests cover it.

// Documents/Pedalboard 3/Recordings.
inline juce::File defaultFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Pedalboard 3")
        .getChildFile("Recordings");
}

// "<patch> yyyy-mm-dd HH-mm-ss", safe as a file name; "Take" stands in for a
// patch with no usable name.
inline juce::String takeName(const juce::String& patchName, const juce::Time& when)
{
    auto base = juce::File::createLegalFileName(patchName.trim()).trim();
    if (base.isEmpty())
        base = "Take";
    return base + " " + when.formatted("%Y-%m-%d %H-%M-%S");
}

// The file for a new take in `folder`: the take name, with " 2", " 3", ...
// added when a file of that name is already there, so two takes started in the
// same second never overwrite each other.
inline juce::File newTakeFile(const juce::File& folder, const juce::String& patchName, const juce::Time& when)
{
    const auto name = takeName(patchName, when);
    auto file = folder.getChildFile(name + ".wav");
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile(name + " " + juce::String(n) + ".wav");
    return file;
}

} // namespace pedalboard3::engine::recordings
