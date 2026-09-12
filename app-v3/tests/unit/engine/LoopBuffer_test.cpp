// Tests for engine::processors::LoopBuffer (M6 U8): chunked loop memory the
// disk thread grows ahead of the audio thread, capped, never allocating on
// write.

#include <catch2/catch_test_macros.hpp>

#include "engine/processors/LoopBuffer.h"

#include <atomic>
#include <thread>

using pedalboard3::engine::processors::LoopBuffer;

namespace
{
constexpr int kChunk = LoopBuffer::kChunkFrames;
}

TEST_CASE("LoopBuffer: nothing is writable until it's reserved", "[engine][processors][looper]")
{
    LoopBuffer loop(4 * static_cast<std::int64_t>(kChunk));
    REQUIRE(loop.capacity() == 0);
    juce::AudioBuffer<float> source(2, 100);
    source.clear();
    REQUIRE(loop.write(0, source, 0, 100) == 0);

    REQUIRE(loop.reserve(10));
    REQUIRE(loop.capacity() == kChunk);
}

TEST_CASE("LoopBuffer: a write across chunks reads back exactly", "[engine][processors][looper]")
{
    LoopBuffer loop(4 * static_cast<std::int64_t>(kChunk));
    REQUIRE(loop.reserve(3 * static_cast<std::int64_t>(kChunk)));
    juce::AudioBuffer<float> source(2, 3000);
    for (int n = 0; n < 3000; ++n)
    {
        source.setSample(0, n, static_cast<float>(n) * 0.001f);
        source.setSample(1, n, static_cast<float>(-n) * 0.001f);
    }

    const std::int64_t at = kChunk - 1000;
    REQUIRE(loop.write(at, source, 500, 2500) == 2500);
    int bad = 0;
    for (int i = 0; i < 2500; ++i)
        for (int ch = 0; ch < 2; ++ch)
            if (loop.get(ch, at + i) != source.getSample(ch, 500 + i))
                ++bad;
    REQUIRE(bad == 0);
}

TEST_CASE("LoopBuffer: the limit caps it, and a write past it writes what fits", "[engine][processors][looper]")
{
    LoopBuffer loop(10000);
    REQUIRE_FALSE(loop.reserve(20000));
    REQUIRE(loop.capacity() == 10000);

    juce::AudioBuffer<float> source(2, 2000);
    source.clear();
    REQUIRE(loop.write(9000, source, 0, 2000) == 1000);
    REQUIRE(loop.write(10000, source, 0, 1) == 0);

    loop.setLimit(5000);
    REQUIRE(loop.capacity() == 5000);
    loop.setLimit(1'000'000); // no further than the table it was sized for
    REQUIRE(loop.limit() == loop.maxFrames());
    REQUIRE(loop.capacity() == kChunk);
}

TEST_CASE("LoopBuffer: a mono source fills both channels", "[engine][processors][looper]")
{
    LoopBuffer loop(kChunk);
    REQUIRE(loop.reserve(100));
    juce::AudioBuffer<float> mono(1, 10);
    for (int n = 0; n < 10; ++n)
        mono.setSample(0, n, static_cast<float>(n + 1));
    REQUIRE(loop.write(0, mono, 0, 10) == 10);
    for (int n = 0; n < 10; ++n)
    {
        REQUIRE(loop.get(0, n) == static_cast<float>(n + 1));
        REQUIRE(loop.get(1, n) == static_cast<float>(n + 1));
    }
}

TEST_CASE("LoopBuffer: one thread reserves ahead while another writes behind it", "[engine][processors][looper]")
{
    constexpr std::int64_t total = 12 * static_cast<std::int64_t>(kChunk);
    constexpr int block = 512;
    LoopBuffer loop(16 * static_cast<std::int64_t>(kChunk));
    std::atomic<std::int64_t> written{0};

    std::thread disk(
        [&]
        {
            while (written.load() < total)
            {
                loop.reserve(written.load() + 2 * static_cast<std::int64_t>(kChunk));
                std::this_thread::yield();
            }
        });

    juce::AudioBuffer<float> source(2, block);
    while (written.load() < total)
    {
        const auto at = written.load();
        if (at + block > loop.capacity())
        {
            std::this_thread::yield();
            continue;
        }
        for (int n = 0; n < block; ++n)
        {
            source.setSample(0, n, static_cast<float>((at + n) % 10007));
            source.setSample(1, n, static_cast<float>((at + n) % 101));
        }
        REQUIRE(loop.write(at, source, 0, block) == block);
        written.store(at + block);
    }
    disk.join();

    int bad = 0;
    for (std::int64_t i = 0; i < total; ++i)
        if (loop.get(0, i) != static_cast<float>(i % 10007) || loop.get(1, i) != static_cast<float>(i % 101))
            ++bad;
    REQUIRE(bad == 0);
}
