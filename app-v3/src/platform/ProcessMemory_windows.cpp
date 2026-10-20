#include "ProcessMemory.h"

// <psapi.h> needs <windows.h> ahead of it, and both want the lean macro so a
// translation unit that includes JUCE elsewhere does not inherit min/max.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>

#include <psapi.h>

namespace pedalboard3::platform
{

std::uint64_t processMemoryBytes()
{
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0)
        return 0;
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
}

} // namespace pedalboard3::platform
