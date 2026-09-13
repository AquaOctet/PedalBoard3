#include "AtomicFileWriterPlatform.h"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace pedalboard3::session::atomicfile
{

bool renameReplace(const char* from, const char* to) noexcept
{
    return std::rename(from, to) == 0;
}

void syncDirectory(const char* folder) noexcept
{
    const int fd = ::open(folder, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return; // the data is already flushed; only the rename's durability is at stake
    ::fsync(fd);
    ::close(fd);
}

} // namespace pedalboard3::session::atomicfile
