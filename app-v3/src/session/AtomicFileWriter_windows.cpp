#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "AtomicFileWriterPlatform.h"

namespace pedalboard3::session::atomicfile
{

bool replaceWriteThrough(const wchar_t* from, const wchar_t* to) noexcept
{
    return MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

} // namespace pedalboard3::session::atomicfile
