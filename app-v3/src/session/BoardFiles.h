#pragma once

#include <juce_core/juce_core.h>

namespace pedalboard3::session::boardfiles
{

// Which files are boards, and which were opened lately (checklist R9: Open
// Recent #30 and drag-and-drop open #31), free of UI so the headless tests
// cover it. Board files are .pdl: v3 banks and Pedalboard2 files alike.

inline constexpr int kMaxRecent = 10;

// The first existing .pdl among dropped paths, or File() when there is none.
inline juce::File firstBoardFile(const juce::StringArray& paths)
{
    for (const auto& path : paths)
        if (juce::File::isAbsolutePath(path))
        {
            const juce::File file(path);
            if (file.hasFileExtension("pdl") && file.existsAsFile())
                return file;
        }
    return {};
}

// File > Open Recent's list (full paths, newest first) with `file` moved to the
// front. An older entry for the same file goes (compared as the file system
// compares, so case-insensitively on Windows and macOS), so does anything that
// isn't an absolute path, and the list is capped.
inline juce::StringArray pushRecent(const juce::StringArray& paths, const juce::File& file, int max = kMaxRecent)
{
    juce::StringArray out;
    out.add(file.getFullPathName());
    for (const auto& path : paths)
        if (out.size() < max && juce::File::isAbsolutePath(path) && juce::File(path) != file)
            out.add(path);
    return out;
}

// The list without `file` (it was picked but has gone).
inline juce::StringArray removeRecent(const juce::StringArray& paths, const juce::File& file)
{
    juce::StringArray out;
    for (const auto& path : paths)
        if (juce::File::isAbsolutePath(path) && juce::File(path) != file)
            out.add(path);
    return out;
}

// An Open Recent item: the board's name, then its folder, so two "Live.pdl" in
// different folders can be told apart.
inline juce::String menuLabel(const juce::File& file)
{
    return file.getFileNameWithoutExtension() + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) +
           file.getParentDirectory().getFileName();
}

} // namespace pedalboard3::session::boardfiles
