#pragma once

namespace pedalboard3::platform
{

// The plugin crash guard (M5 U5, plan KD1, master plan AC-PL-1): a hardware
// fault inside a guarded call (an access violation, a stack overflow, an
// illegal instruction, a divide by zero) is caught and reported instead of
// taking the host down.
//
//   - Windows: a C-style __try / __except shim (CrashGuard_windows.cpp).
//   - Linux: handlers for SIGSEGV, SIGBUS, SIGFPE and SIGILL that jump back to
//     the guarded call (CrashGuard_posix.cpp). A fault outside a guarded call
//     goes on to the handler that was there before, so a crash reporter still
//     sees real crashes.
//   - macOS: off (plan KD1). The call is made unguarded.
//
// RT-safe: no allocation and no locks. On Linux the first guarded call on a
// thread gives that thread an alternate signal stack, held in thread-local
// storage (one sigaltstack call).
//
// After a fault, the code that was interrupted never finished: its locks may
// still be held and its destructors never ran. Whatever faulted must not be
// called again (BypassableInstance leaks a crashed plugin rather than touch it).

enum class Fault
{
    None,
    AccessViolation,
    StackOverflow,
    IllegalInstruction,
    DivideByZero,
    FloatingPoint,
    BusError,
    Exception, // a C++ exception out of a plugin, caught by the caller's own try
};

// How a card names a fault.
constexpr const char* faultName(Fault fault) noexcept
{
    switch (fault)
    {
    case Fault::None:
        return "";
    case Fault::AccessViolation:
        return "access violation";
    case Fault::StackOverflow:
        return "stack overflow";
    case Fault::IllegalInstruction:
        return "illegal instruction";
    case Fault::DivideByZero:
        return "divide by zero";
    case Fault::FloatingPoint:
        return "floating-point error";
    case Fault::BusError:
        return "bus error";
    case Fault::Exception:
        return "unhandled exception";
    }
    return "crash";
}

// Whether this platform has the guard (false on macOS).
bool crashGuardAvailable() noexcept;

// Install the Linux signal handlers (a no-op elsewhere). Call once at startup,
// before any audio runs. Calling it again puts them back if something replaced
// them, keeping that as the handler a fault outside a guarded call goes to.
void installCrashGuard() noexcept;

// Call fn(context), catching a hardware fault inside it: Fault::None when it
// returned normally. fn must not throw. Not reentrant on one thread.
Fault guardedCall(void (*fn)(void*), void* context) noexcept;

// guardedCall for a callable object. Calling it must not throw.
template <typename Callable>
Fault guarded(Callable& callable) noexcept
{
    return guardedCall([](void* context) { (*static_cast<Callable*>(context))(); }, &callable);
}

} // namespace pedalboard3::platform
