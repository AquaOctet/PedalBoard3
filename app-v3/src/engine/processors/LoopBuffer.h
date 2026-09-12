#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace pedalboard3::engine::processors
{

// A Looper's memory (M6 U8, plan KD8): stereo audio in fixed-size chunks, so a
// loop can grow for minutes without the audio thread ever allocating.
//   - One owner thread (the disk thread) allocates chunks with reserve(),
//     ahead of the recording. A chunk is published with a release store, and
//     then never moves or goes away while the buffer lives.
//   - The audio thread reads and writes only below capacity(). A write past it
//     writes what fits and says how much, and the caller stops recording.
//   - setLimit caps the loop (a Looper's is 10 minutes at the device rate).
//     The chunk table is sized once, for maxFrames.
class LoopBuffer
{
public:
    static constexpr int kChannels = 2;
    static constexpr int kChunkFrames = 1 << 16; // 1.4 s at 48 kHz

    explicit LoopBuffer(std::int64_t maxFrames)
        : chunks_(static_cast<std::size_t>((juce::jmax<std::int64_t>(1, maxFrames) + kChunkFrames - 1) / kChunkFrames)),
          limit_(juce::jmax<std::int64_t>(1, maxFrames))
    {
    }

    // The most frames it could ever hold, and the cap in force (any thread).
    std::int64_t maxFrames() const noexcept { return static_cast<std::int64_t>(chunks_.size()) * kChunkFrames; }
    std::int64_t limit() const noexcept { return limit_.load(std::memory_order_acquire); }
    void setLimit(std::int64_t frames) noexcept
    {
        limit_.store(juce::jlimit<std::int64_t>(1, maxFrames(), frames), std::memory_order_release);
    }

    // The owner thread: make [0, frames) writable, up to the limit. False when
    // the limit cut it short.
    bool reserve(std::int64_t frames)
    {
        const auto wanted = juce::jmin(frames, limit());
        const auto needed = static_cast<int>((wanted + kChunkFrames - 1) / kChunkFrames);
        int ready = ready_.load(std::memory_order_relaxed);
        while (ready < needed)
        {
            chunks_[static_cast<std::size_t>(ready)] =
                std::make_unique<float[]>(static_cast<std::size_t>(kChunkFrames) * kChannels); // zeroed
            ready_.store(++ready, std::memory_order_release);
        }
        return wanted >= frames;
    }

    // How many frames can be read and written now (any thread).
    std::int64_t capacity() const noexcept
    {
        return juce::jmin(static_cast<std::int64_t>(ready_.load(std::memory_order_acquire)) * kChunkFrames, limit());
    }

    // Copies numFrames frames from `source` (a mono source fills both
    // channels) to `at`, as many as fit below capacity(); returns how many.
    int write(std::int64_t at, const juce::AudioBuffer<float>& source, int sourceStart, int numFrames) noexcept
    {
        const int channels = juce::jmin(kChannels, source.getNumChannels());
        if (channels == 0 || at < 0)
            return 0;
        const int total = static_cast<int>(juce::jlimit<std::int64_t>(0, numFrames, capacity() - at));
        int done = 0;
        while (done < total)
        {
            const auto frame = at + done;
            const int offset = static_cast<int>(frame % kChunkFrames);
            const int count = juce::jmin(total - done, kChunkFrames - offset);
            float* chunk = chunks_[static_cast<std::size_t>(frame / kChunkFrames)].get();
            for (int ch = 0; ch < kChannels; ++ch)
                juce::FloatVectorOperations::copy(
                    chunk + ch * kChunkFrames + offset,
                    source.getReadPointer(juce::jmin(ch, channels - 1), sourceStart + done),
                    count);
            done += count;
        }
        return total;
    }

    // One sample; `at` must be below capacity().
    float get(int channel, std::int64_t at) const noexcept
    {
        const float* chunk = chunks_[static_cast<std::size_t>(at / kChunkFrames)].get();
        return chunk[static_cast<std::size_t>(channel) * kChunkFrames + static_cast<std::size_t>(at % kChunkFrames)];
    }

private:
    std::vector<std::unique_ptr<float[]>> chunks_;
    std::atomic<int> ready_{0};
    std::atomic<std::int64_t> limit_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopBuffer)
};

} // namespace pedalboard3::engine::processors
