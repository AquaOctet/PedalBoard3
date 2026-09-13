// The crash guard's Windows half (M5 U5, plan KD1): structured exception
// handling. MSVC won't compile __try in a function that holds C++ objects with
// destructors, so guardedCall holds nothing but plain values.

#include "CrashGuard.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <malloc.h> // _resetstkoflw

namespace pedalboard3::platform
{

namespace
{

// The exceptions the guard catches, and what it calls them. Anything else (a
// C++ exception, a breakpoint, a debugger's) goes on up.
int filter(unsigned long code, Fault* fault) noexcept
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        *fault = Fault::AccessViolation;
        break;
    case EXCEPTION_STACK_OVERFLOW:
        *fault = Fault::StackOverflow;
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        *fault = Fault::IllegalInstruction;
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
        *fault = Fault::DivideByZero;
        break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
        *fault = Fault::FloatingPoint;
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

bool crashGuardAvailable() noexcept
{
    return true;
}

void installCrashGuard() noexcept
{
    // Structured exception handling needs nothing installed.
}

Fault guardedCall(void (*fn)(void*), void* context) noexcept
{
    Fault fault = Fault::None;
    __try
    {
        fn(context);
    }
    __except (filter(GetExceptionCode(), &fault))
    {
        // A stack overflow used up the stack's guard page. Put it back, or the
        // next overflow ends the process.
        if (fault == Fault::StackOverflow)
            _resetstkoflw();
    }
    return fault;
}

} // namespace pedalboard3::platform
