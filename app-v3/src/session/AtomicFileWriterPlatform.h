#pragma once

// The platform half of AtomicFileWriter (M5 U2), kept apart from JUCE's headers
// so the Windows file can include <windows.h> on its own.
namespace pedalboard3::session::atomicfile
{

#if defined(_WIN32)
// MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH): returns once the move is on
// the disk.
bool replaceWriteThrough(const wchar_t* from, const wchar_t* to) noexcept;
#else
// rename(2): atomic within a folder.
bool renameReplace(const char* from, const char* to) noexcept;
// fsync the folder, so a rename in it survives a power cut.
void syncDirectory(const char* folder) noexcept;
// chmod 0600: readable and writable by the owner alone (M5 U7).
void restrictToOwner(const char* path) noexcept;
#endif

} // namespace pedalboard3::session::atomicfile
