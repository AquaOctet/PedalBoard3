#include "ProcessMemory.h"

namespace pedalboard3::platform
{

// No supported way to ask on this platform; the meter reads a dash.
std::uint64_t processMemoryBytes()
{
    return 0;
}

} // namespace pedalboard3::platform
