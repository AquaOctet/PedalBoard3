#include "AtomicFileWriter.h"

#include "AtomicFileWriterPlatform.h"

namespace pedalboard3::session
{

juce::Result AtomicFileWriter::write(const juce::File& target,
                                     const std::function<bool(juce::OutputStream&)>& body,
                                     const StepHook& hook,
                                     [[maybe_unused]] bool ownerOnly)
{
    const auto folder = target.getParentDirectory();
    if (!folder.isDirectory())
    {
        const auto made = folder.createDirectory();
        if (made.failed())
            return juce::Result::fail("Couldn't create " + folder.getFullPathName() + ": " + made.getErrorMessage());
    }

    // Beside the target, so the replace is a rename within one folder.
    const auto temp = folder.getNonexistentChildFile(
        target.getFileNameWithoutExtension() + ".saving", target.getFileExtension() + ".tmp", false);

    juce::String error;
    {
        juce::FileOutputStream out(temp);
        if (out.failedToOpen())
        {
            error = "Couldn't write " + target.getFullPathName() + ": " + out.getStatus().getErrorMessage();
        }
        else if (!body(out) || out.getStatus().failed())
        {
            error = "Couldn't write " + target.getFullPathName();
        }
        else if (hook && !hook(Step::Written))
        {
            error = "Stopped after writing";
        }
        else
        {
            out.flush(); // to the disk: FlushFileBuffers / fsync
            if (out.getStatus().failed())
                error = "Couldn't write " + target.getFullPathName() + ": " + out.getStatus().getErrorMessage();
            else if (hook && !hook(Step::Flushed))
                error = "Stopped after flushing";
        }
    } // closed here, so the temp file can be removed (Windows won't delete an open file) or moved

    if (error.isNotEmpty())
    {
        temp.deleteFile();
        return juce::Result::fail(error);
    }

#if !JUCE_WINDOWS
    if (ownerOnly)
        atomicfile::restrictToOwner(temp.getFullPathName().toRawUTF8());
#endif

#if JUCE_WINDOWS
    const bool replaced = atomicfile::replaceWriteThrough(temp.getFullPathName().toWideCharPointer(),
                                                          target.getFullPathName().toWideCharPointer());
#else
    const bool replaced =
        atomicfile::renameReplace(temp.getFullPathName().toRawUTF8(), target.getFullPathName().toRawUTF8());
#endif
    if (!replaced)
    {
        temp.deleteFile();
        return juce::Result::fail("Couldn't replace " + target.getFullPathName());
    }
    if (hook && !hook(Step::Replaced))
        return juce::Result::fail("Stopped after replacing");

#if !JUCE_WINDOWS
    atomicfile::syncDirectory(folder.getFullPathName().toRawUTF8());
#endif
    return juce::Result::ok();
}

juce::Result AtomicFileWriter::writeText(const juce::File& target,
                                         const juce::String& text,
                                         const StepHook& hook,
                                         bool ownerOnly)
{
    return write(
        target,
        [&text](juce::OutputStream& out) { return out.writeText(text, false, false, nullptr); },
        hook,
        ownerOnly);
}

} // namespace pedalboard3::session
