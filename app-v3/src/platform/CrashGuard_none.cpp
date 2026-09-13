// No crash guard on macOS (M5 U5, plan KD1): Mach exceptions and the debugger
// make recovering from a signal unreliable there, and the M8 crash reporter
// covers it. A guarded call is an ordinary call.

#include "CrashGuard.h"

namespace pedalboard3::platform
{

bool crashGuardAvailable() noexcept
{
    return false;
}

void installCrashGuard() noexcept {}

Fault guardedCall(void (*fn)(void*), void* context) noexcept
{
    fn(context);
    return Fault::None;
}

} // namespace pedalboard3::platform
