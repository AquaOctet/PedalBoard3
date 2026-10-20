#include "ProcessMemory.h"

#include <cstdio>
#include <unistd.h>

namespace pedalboard3::platform
{

std::uint64_t processMemoryBytes()
{
    // /proc/self/statm's second field is the resident set in pages. Reading
    // statm rather than status is deliberate: statm is a short line of
    // numbers, so this is one read and one scan, where VmRSS in status means
    // walking dozens of lines looking for a label.
    std::FILE* f = std::fopen("/proc/self/statm", "re");
    if (f == nullptr)
        return 0;

    unsigned long long totalPages = 0;
    unsigned long long residentPages = 0;
    const int matched = std::fscanf(f, "%llu %llu", &totalPages, &residentPages);
    std::fclose(f);
    if (matched != 2)
        return 0;

    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return 0;
    return static_cast<std::uint64_t>(residentPages) * static_cast<std::uint64_t>(pageSize);
}

} // namespace pedalboard3::platform
