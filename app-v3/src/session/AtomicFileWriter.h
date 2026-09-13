#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace pedalboard3::session
{

// Writes a file so that a crash or power cut at any moment leaves either the
// old file or the new one, never a torn one (M5 U2, master plan AC-FS-6 and
// §A.5):
//   1. Write a temp file beside the target. It's in the same folder, so the
//      replace is a rename.
//   2. Flush it to the disk (FlushFileBuffers / fsync).
//   3. Replace the target with it (MoveFileExW with WRITE_THROUGH / rename).
//   4. On POSIX, fsync the folder, so the rename itself is on the disk.
// Any failure removes the temp file and leaves the target as it was.
//
// Any thread; one writer per target at a time.
class AtomicFileWriter
{
public:
    enum class Step
    {
        Written,
        Flushed,
        Replaced,
    };

    // Tests: returning false stops the write right after that step, as a crash
    // there would.
    using StepHook = std::function<bool(Step)>;

    // `ownerOnly` (M5 U7, master plan AC-SE-24): on POSIX the file ends up
    // readable and writable by its owner alone (0600), set on the temp file
    // before it replaces the target. Windows keeps the folder's permissions (a
    // user's %APPDATA% is theirs already).
    static juce::Result write(const juce::File& target,
                              const std::function<bool(juce::OutputStream&)>& body,
                              const StepHook& hook = {},
                              bool ownerOnly = false);
    static juce::Result writeText(const juce::File& target,
                                  const juce::String& text,
                                  const StepHook& hook = {},
                                  bool ownerOnly = false);
};

} // namespace pedalboard3::session
