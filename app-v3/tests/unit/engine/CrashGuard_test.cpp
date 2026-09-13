// Tests for platform::guardedCall and BypassableInstance's crash guard (M5 U5,
// plan KD1 / KD2, master plan AC-PL-1): a fault inside a plugin is caught, its
// slot passes the input through, and the host carries on. Windows and Linux;
// skipped on macOS, where the guard is off.

#include <catch2/catch_test_macros.hpp>

#include "engine/BypassableInstance.h"
#include "platform/CrashGuard.h"
#include "support/CrashingProcessor.h"
#include "support/OfflineRender.h"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>

#if JUCE_LINUX
#include <setjmp.h>
#include <signal.h>
#endif

using namespace pedalboard3;
using namespace pedalboard3::testing;

// ---- Counting allocations, for the RT-safety test ---------------------------
// The test binary's operator new counts while `countAllocations` is set on the
// calling thread.

namespace
{
thread_local bool countAllocations = false;
std::atomic<int> allocations{0};
} // namespace

void* operator new(std::size_t size)
{
    if (countAllocations)
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* block = std::malloc(size == 0 ? 1 : size))
        return block;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* block) noexcept
{
    std::free(block);
}

void operator delete[](void* block) noexcept
{
    std::free(block);
}

void operator delete(void* block, std::size_t) noexcept
{
    std::free(block);
}

void operator delete[](void* block, std::size_t) noexcept
{
    std::free(block);
}

namespace
{

// Catch2 puts in its own signal handlers for each test case (POSIX), so the
// guard goes back in for each one, as the app installs it at startup.
bool guardIsOn()
{
    platform::installCrashGuard();
    return platform::crashGuardAvailable();
}

void nullWrite(void*)
{
    writeThroughNull();
}

void setFlag(void* flag)
{
    *static_cast<bool*>(flag) = true;
}

volatile int zeroDivisor = 0;

[[maybe_unused]] void divideByZero(void*)
{
    volatile int result = 1 / zeroDivisor;
    (void)result;
}

#if JUCE_WINDOWS
// Deep enough to use up the stack, and not something the compiler can flatten
// into a loop.
int recurseDeeper(volatile int depth)
{
    volatile char frame[4096];
    frame[0] = static_cast<char>(depth);
    return depth >= 0 ? recurseDeeper(depth + 1) + frame[0] : 0;
}

void overflowStack(void*)
{
    (void)recurseDeeper(0);
}
#endif

std::unique_ptr<engine::BypassableInstance> wrap(std::unique_ptr<juce::AudioProcessor> inner)
{
    inner->setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    auto wrapper = std::make_unique<engine::BypassableInstance>(std::move(inner));
    wrapper->setPlayConfigDetails(2, 2, kDefaultSampleRate, kDefaultBlockSize);
    wrapper->prepareToPlay(kDefaultSampleRate, kDefaultBlockSize);
    return wrapper;
}

bool isDC(const juce::AudioBuffer<float>& buffer, float value)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (buffer.getSample(ch, n) != value)
                return false;
    return true;
}

} // namespace

TEST_CASE("CrashGuard: a write through a null pointer is caught, and the next call runs", "[engine][crash-guard]")
{
    if (!guardIsOn())
        SKIP("The crash guard is off on macOS (plan KD1)");

    REQUIRE(platform::guardedCall(nullWrite, nullptr) == platform::Fault::AccessViolation);

    bool ran = false;
    REQUIRE(platform::guardedCall(setFlag, &ran) == platform::Fault::None);
    REQUIRE(ran);

    // The guard isn't used up by a catch.
    REQUIRE(platform::guardedCall(nullWrite, nullptr) == platform::Fault::AccessViolation);
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
TEST_CASE("CrashGuard: an integer divide by zero is caught", "[engine][crash-guard]")
{
    if (!guardIsOn())
        SKIP("The crash guard is off on macOS (plan KD1)");
    REQUIRE(platform::guardedCall(divideByZero, nullptr) == platform::Fault::DivideByZero);
}
#endif

#if JUCE_WINDOWS
TEST_CASE("CrashGuard: a stack overflow is caught, and so is the next one (Windows)", "[engine][crash-guard]")
{
    REQUIRE(platform::guardedCall(overflowStack, nullptr) == platform::Fault::StackOverflow);
    // _resetstkoflw put the guard page back, so a second overflow is caught too.
    REQUIRE(platform::guardedCall(overflowStack, nullptr) == platform::Fault::StackOverflow);
}
#endif

#if JUCE_LINUX
namespace
{
// `struct sigaction` by another name, so its declarations format the same
// with every clang-format version.
using SigAction = struct sigaction;

sigjmp_buf previousJump;
volatile sig_atomic_t previousHandlerRan = 0;

void previousHandler(int)
{
    previousHandlerRan = 1;
    siglongjmp(previousJump, 1);
}

// A crash outside any guarded call; true once the previous handler has taken it.
bool crashOutsideTheGuard()
{
    if (sigsetjmp(previousJump, 1) == 0)
    {
        writeThroughNull();
        return false;
    }
    return true;
}
} // namespace

TEST_CASE("CrashGuard: after a catch, a crash outside a guarded call still reaches the previous handler (Linux)",
          "[engine][crash-guard]")
{
    SigAction catchHandler{};
    sigaction(SIGSEGV, nullptr, &catchHandler);

    // A handler that was there before the guard, as a crash reporter would be.
    SigAction previous{};
    previous.sa_handler = previousHandler;
    sigemptyset(&previous.sa_mask);
    previous.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &previous, nullptr);
    platform::installCrashGuard();

    REQUIRE(platform::guardedCall(nullWrite, nullptr) == platform::Fault::AccessViolation);
    REQUIRE(previousHandlerRan == 0);

    REQUIRE(crashOutsideTheGuard());
    REQUIRE(previousHandlerRan == 1);

    sigaction(SIGSEGV, &catchHandler, nullptr); // Catch2's again
}
#endif

TEST_CASE("CrashGuard: a plugin that crashes mid-block passes its input through, and isn't called again",
          "[engine][crash-guard][bypassable]")
{
    if (!guardIsOn())
        SKIP("The crash guard is off on macOS (plan KD1)");

    auto inner = std::make_unique<CrashingProcessor>(/*crashOnBlock*/ 1);
    auto* crashing = inner.get();
    auto wrapper = wrap(std::move(inner));
    const auto deathsBefore = engine::BypassableInstance::deathCount();

    auto first = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(first, 0.25f);
    renderBlocks(*wrapper, first); // block 0 is fine
    REQUIRE_FALSE(wrapper->isDead());
    REQUIRE(isDC(first, 0.25f));

    auto crash = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(crash, 0.25f);
    renderBlocks(*wrapper, crash); // block 1 crashes
    REQUIRE(wrapper->isDead());
    REQUIRE(wrapper->hasCrashed());
    REQUIRE(wrapper->getFault() == platform::Fault::AccessViolation);
    REQUIRE(isDC(crash, 0.25f)); // the input as it came in, not what it scribbled
    REQUIRE(engine::BypassableInstance::deathCount() == deathsBefore + 1);

    auto after = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(after, 0.5f);
    renderBlocks(*wrapper, after);
    REQUIRE(isDC(after, 0.5f));
    REQUIRE(crashing->blocks.load() == 2); // never called again

    // A crash isn't cleared in place: only a fresh instance brings it back
    // (Session::rearmNode). Nothing else reaches it either.
    wrapper->clearDeath();
    REQUIRE(wrapper->hasCrashed());
    REQUIRE(wrapper->createEditor() == nullptr);
    juce::MemoryBlock state;
    REQUIRE_FALSE(wrapper->captureState(state));
}

TEST_CASE("CrashGuard: a guarded block, MIDI channel filter included, doesn't allocate",
          "[engine][crash-guard][rt-safety]")
{
    guardIsOn(); // on Linux this thread's first guarded call sets up its signal stack
    auto wrapper = wrap(std::make_unique<TestGainProcessor>(1.0f));
    wrapper->setMidiChannel(3);

    auto buffer = makeTestBuffer(2, kDefaultBlockSize);
    juce::MidiBuffer midi;
    midi.ensureSize(1024);
    const auto fillMidi = [&midi]
    {
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(3, 60, static_cast<juce::uint8>(100)), 0);
        midi.addEvent(juce::MidiMessage::noteOn(11, 60, static_cast<juce::uint8>(100)), 10);
    };

    fillMidi();
    wrapper->processBlock(buffer, midi); // warm-up

    fillMidi();
    allocations.store(0);
    countAllocations = true;
    wrapper->processBlock(buffer, midi);
    countAllocations = false;

    REQUIRE(allocations.load() == 0);
    auto* inner = dynamic_cast<TestGainProcessor*>(wrapper->getHostedProcessor());
    REQUIRE(inner != nullptr);
    REQUIRE(inner->lastMidiEventCount.load() == 1); // the channel 3 note only
}
