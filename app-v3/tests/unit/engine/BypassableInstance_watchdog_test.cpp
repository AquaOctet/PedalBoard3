// Tests for pedalboard3::engine::BypassableInstance's block-budget
// watchdog. 3 consecutive overruns mark the plugin dead; a subsequent
// healthy block resets the counter. Dead plugins output silence and never
// invoke the hosted processor. clearDeath brings them back.
//
// Plan: Appendix A.2; Phase 1.B.1; AC-PL-1.

#include <catch2/catch_test_macros.hpp>

#include "engine/BypassableInstance.h"
#include "support/OfflineRender.h"
#include "support/SlowProcessor.h"

#include <cstdint>
#include <limits>
#include <memory>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{
struct Harness
{
    SlowProcessor* inner = nullptr;
    std::unique_ptr<engine::BypassableInstance> wrapper;
};

Harness makeHarness(int blockSize = kDefaultBlockSize)
{
    auto slow = std::make_unique<SlowProcessor>();
    auto* slowRaw = slow.get();
    slow->setPlayConfigDetails(2, 2, kDefaultSampleRate, blockSize);

    auto wrapper = std::make_unique<engine::BypassableInstance>(std::move(slow));
    wrapper->setPlayConfigDetails(2, 2, kDefaultSampleRate, blockSize);
    wrapper->prepareToPlay(kDefaultSampleRate, blockSize);
    return {slowRaw, std::move(wrapper)};
}
} // namespace

TEST_CASE("BypassableInstance watchdog: default budget is derived from sample rate + block size",
          "[engine][bypassable][watchdog]")
{
    auto h = makeHarness();
    // Budget defaults to 75% of one block's wall-clock time. At 48k/256
    // that's ~4 ms; exact value not tested here (implementation detail) —
    // we just assert "budget > 0" and the fields are reachable.
    REQUIRE(h.wrapper->getBlockBudgetNanos() > 0);
    REQUIRE_FALSE(h.wrapper->isDead());
}

TEST_CASE("BypassableInstance watchdog: healthy plugin never gets marked dead", "[engine][bypassable][watchdog]")
{
    auto h = makeHarness();
    // Zero-sleep inner — trivially under budget.
    h.inner->sleepNanos.store(0);

    auto buf = makeTestBuffer(2, kDefaultBlockSize * 20);
    renderBlocks(*h.wrapper, buf);

    REQUIRE_FALSE(h.wrapper->isDead());
    // Hosted processor ran for every block.
    REQUIRE(h.inner->processBlockCallCount.load() == 20);
}

TEST_CASE("BypassableInstance watchdog: a single overrun does NOT mark the plugin dead",
          "[engine][bypassable][watchdog]")
{
    auto h = makeHarness();
    // Shrink the budget so we can induce overrun via a cheap sleep. Any
    // processBlock that takes >1 ns is an overrun at budget=0.
    h.wrapper->setBlockBudgetNanos(0);

    // First block: overrun. Second + third blocks: restore healthy budget.
    h.inner->sleepNanos.store(0);
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    renderBlocks(*h.wrapper, buf); // 1st block — overrun (budget 0)

    REQUIRE_FALSE(h.wrapper->isDead());

    // Raise the budget back to "infinity" so subsequent blocks are healthy.
    h.wrapper->setBlockBudgetNanos(std::numeric_limits<std::uint64_t>::max());

    auto healthy = makeTestBuffer(2, kDefaultBlockSize * 2);
    renderBlocks(*h.wrapper, healthy);

    REQUIRE_FALSE(h.wrapper->isDead());
}

TEST_CASE("BypassableInstance watchdog: three consecutive overruns mark the plugin dead",
          "[engine][bypassable][watchdog]")
{
    auto h = makeHarness();
    h.wrapper->setBlockBudgetNanos(0); // every block overruns

    auto buf = makeTestBuffer(2, kDefaultBlockSize * 3);
    renderBlocks(*h.wrapper, buf);

    REQUIRE(h.wrapper->isDead());
}

TEST_CASE("BypassableInstance watchdog: a healthy block resets the consecutive-overrun counter",
          "[engine][bypassable][watchdog]")
{
    auto h = makeHarness();
    h.wrapper->setBlockBudgetNanos(0);

    // 2 overruns — not dead yet.
    auto two = makeTestBuffer(2, kDefaultBlockSize * 2);
    renderBlocks(*h.wrapper, two);
    REQUIRE_FALSE(h.wrapper->isDead());

    // Now go healthy (infinite budget) for one block. Counter should reset.
    h.wrapper->setBlockBudgetNanos(std::numeric_limits<std::uint64_t>::max());
    auto one = makeTestBuffer(2, kDefaultBlockSize);
    renderBlocks(*h.wrapper, one);
    REQUIRE_FALSE(h.wrapper->isDead());

    // Back to overrun — need 3 fresh in a row.
    h.wrapper->setBlockBudgetNanos(0);
    auto two2 = makeTestBuffer(2, kDefaultBlockSize * 2);
    renderBlocks(*h.wrapper, two2);
    REQUIRE_FALSE(h.wrapper->isDead()); // only 2 — still alive

    auto one2 = makeTestBuffer(2, kDefaultBlockSize);
    renderBlocks(*h.wrapper, one2);
    REQUIRE(h.wrapper->isDead()); // now 3 in a row
}

TEST_CASE("BypassableInstance watchdog: dead plugin outputs silence and skips hosted processor",
          "[engine][bypassable][watchdog]")
{
    auto h = makeHarness();
    h.wrapper->setBlockBudgetNanos(0);

    auto trip = makeTestBuffer(2, kDefaultBlockSize * 3);
    renderBlocks(*h.wrapper, trip);
    REQUIRE(h.wrapper->isDead());

    const int callCountAtDeath = h.inner->processBlockCallCount.load();

    // Post-death block. SlowProcessor would fill the buffer with 0.5
    // sentinels if called; silence means the dead-path took over.
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    fillDC(buf, 0.25f); // start non-silent to prove the wrapper clears
    renderBlocks(*h.wrapper, buf);

    REQUIRE(isSilent(buf));
    REQUIRE(h.inner->processBlockCallCount.load() == callCountAtDeath);
}

TEST_CASE("BypassableInstance watchdog: clearDeath resets the flag and hosted processor runs again",
          "[engine][bypassable][watchdog]")
{
    auto h = makeHarness();
    h.wrapper->setBlockBudgetNanos(0);

    auto trip = makeTestBuffer(2, kDefaultBlockSize * 3);
    renderBlocks(*h.wrapper, trip);
    REQUIRE(h.wrapper->isDead());

    h.wrapper->setBlockBudgetNanos(std::numeric_limits<std::uint64_t>::max());
    h.wrapper->clearDeath();
    REQUIRE_FALSE(h.wrapper->isDead());

    const int callsBefore = h.inner->processBlockCallCount.load();
    auto buf = makeTestBuffer(2, kDefaultBlockSize);
    renderBlocks(*h.wrapper, buf);
    REQUIRE(h.inner->processBlockCallCount.load() == callsBefore + 1);
}
