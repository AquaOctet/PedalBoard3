#pragma once

#include <cstdint>

namespace pedalboard3::platform
{

// How much memory this process is actually holding in RAM, in bytes, or 0
// when the platform will not say.
//
// This is the resident set — the working set on Windows, resident_size on
// macOS, VmRSS on Linux — and not the virtual size. Virtual size on a plugin
// host is close to meaningless: a sampler that memory-maps ten gigabytes of
// library reserves all of it and touches almost none, and a meter reading
// the reservation would be alarming and wrong. The resident set is what the
// machine is really being asked for, which is what makes it the number worth
// putting next to the CPU load (#118).
//
// Cheap enough for a 1 Hz poll on the message thread: one syscall on each
// platform, no allocation. Not RT-safe and never called from the audio
// thread.
//
//   - Windows: GetProcessMemoryInfo (ProcessMemory_windows.cpp).
//   - macOS: task_info / MACH_TASK_BASIC_INFO (ProcessMemory_mac.cpp).
//   - Linux: /proc/self/statm (ProcessMemory_linux.cpp).
//   - anything else: 0, and the meter reads a dash.
std::uint64_t processMemoryBytes();

} // namespace pedalboard3::platform
