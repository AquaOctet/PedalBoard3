#include "ProcessMemory.h"

#include <mach/mach.h>

namespace pedalboard3::platform
{

std::uint64_t processMemoryBytes()
{
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    return static_cast<std::uint64_t>(info.resident_size);
}

} // namespace pedalboard3::platform
