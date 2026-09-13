// The crash guard's Linux half (M5 U5, plan KD1): signal handlers that jump
// back to the guarded call. The handler touches only thread-local state and the
// handlers saved at install time, and calls only async-signal-safe functions.

#include "CrashGuard.h"

#include <pthread.h>
#include <setjmp.h>
#include <signal.h>

#include <array>
#include <cstddef>

namespace pedalboard3::platform
{

namespace
{

constexpr std::array<int, 4> kSignals{SIGSEGV, SIGBUS, SIGFPE, SIGILL};

// Room for the handler after a stack overflow has used up the thread's stack.
constexpr std::size_t kAltStackBytes = 64 * 1024;

// The handlers that were there before ours, one per signal: a fault outside a
// guarded call goes to them.
std::array<struct sigaction, kSignals.size()> previousHandlers{};

// One per thread. Everything guardedCall needs after the jump lives here rather
// than in its locals, which a longjmp may clobber.
struct ThreadGuard
{
    sigjmp_buf jump;
    void (*fn)(void*);
    void* context;
    volatile sig_atomic_t armed;
    volatile sig_atomic_t caughtSignal;
    volatile sig_atomic_t caughtCode;
    bool altStackReady;
};
thread_local ThreadGuard threadGuard{};
alignas(16) thread_local char altStack[kAltStackBytes];

std::size_t indexOf(int sig) noexcept
{
    for (std::size_t i = 0; i < kSignals.size(); ++i)
        if (kSignals[i] == sig)
            return i;
    return 0;
}

// Not ours: hand the fault to the handler that was there before. If that one
// returns, or there was none, the fault does what it would have done without
// us: the default action, which ends the process.
void passOn(int sig, siginfo_t* info, void* ucontext)
{
    const auto& previous = previousHandlers[indexOf(sig)];
    if ((previous.sa_flags & SA_SIGINFO) != 0)
    {
        if (previous.sa_sigaction != nullptr)
            previous.sa_sigaction(sig, info, ucontext);
    }
    else if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN)
    {
        previous.sa_handler(sig);
    }

    struct sigaction fallback{};
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(sig, &fallback, nullptr);
    raise(sig);
}

void onFault(int sig, siginfo_t* info, void* ucontext)
{
    if (threadGuard.armed != 0)
    {
        threadGuard.armed = 0;
        threadGuard.caughtSignal = sig;
        threadGuard.caughtCode = info != nullptr ? info->si_code : 0;
        siglongjmp(threadGuard.jump, 1);
    }
    passOn(sig, info, ucontext);
}

bool isOurs(const struct sigaction& action) noexcept
{
    return (action.sa_flags & SA_SIGINFO) != 0 && action.sa_sigaction == &onFault;
}

Fault faultFor(int sig, int code) noexcept
{
    switch (sig)
    {
    case SIGSEGV:
        return Fault::AccessViolation;
    case SIGBUS:
        return Fault::BusError;
    case SIGILL:
        return Fault::IllegalInstruction;
    case SIGFPE:
        return (code == FPE_INTDIV || code == FPE_INTOVF) ? Fault::DivideByZero : Fault::FloatingPoint;
    default:
        return Fault::AccessViolation;
    }
}

// Give this thread an alternate signal stack, once, so a stack overflow can
// still be caught. A thread that already has one keeps it.
void ensureAltStack() noexcept
{
    if (threadGuard.altStackReady)
        return;
    threadGuard.altStackReady = true;

    stack_t current{};
    if (sigaltstack(nullptr, &current) == 0 && (current.ss_flags & SS_DISABLE) == 0)
        return;

    stack_t stack{};
    stack.ss_sp = altStack;
    stack.ss_size = sizeof(altStack);
    stack.ss_flags = 0;
    sigaltstack(&stack, nullptr);
}

void unblock(int sig) noexcept
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
}

} // namespace

bool crashGuardAvailable() noexcept
{
    return true;
}

void installCrashGuard() noexcept
{
    for (std::size_t i = 0; i < kSignals.size(); ++i)
    {
        struct sigaction current{};
        if (sigaction(kSignals[i], nullptr, &current) == 0 && isOurs(current))
            continue;

        struct sigaction ours{};
        ours.sa_sigaction = &onFault;
        sigemptyset(&ours.sa_mask);
        // On the alternate stack, so a stack overflow can still be handled; and
        // not deferred, so the signal isn't left blocked after the jump.
        ours.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigaction(kSignals[i], &ours, &previousHandlers[i]);
    }
}

Fault guardedCall(void (*fn)(void*), void* context) noexcept
{
    ensureAltStack();
    threadGuard.fn = fn;
    threadGuard.context = context;

    if (sigsetjmp(threadGuard.jump, 0) != 0)
    {
        // Back from onFault. SA_NODEFER left the signal unblocked, but make
        // sure: a second fault while it's blocked would end the process.
        unblock(threadGuard.caughtSignal);
        return faultFor(threadGuard.caughtSignal, threadGuard.caughtCode);
    }

    threadGuard.armed = 1;
    threadGuard.fn(threadGuard.context);
    threadGuard.armed = 0;
    return Fault::None;
}

} // namespace pedalboard3::platform
