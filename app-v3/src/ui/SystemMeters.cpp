#include "SystemMeters.h"

#include "ColourScheme.h"
#include "MeterRow.h"
#include "platform/ProcessMemory.h"

#include <juce_core/juce_core.h>

namespace pedalboard3::ui
{

namespace
{

constexpr int kRefreshHz = 10;
constexpr int kMemoryEveryTicks = 10; // so memory lands at 1 Hz
constexpr int kCaptionWidth = 34;
constexpr int kNumberWidth = 44;

// Where the memory track starts and where its warnings sit.
//
// The track is logarithmic between a floor and the machine's RAM, and it
// has to be. Linear against total RAM, this app at 139 MB on a 32 GB box
// fills 0.4% of the bar -- a meter that cannot move is worse than no
// meter, because it reads as a fault. Memory on a plugin host spans three
// orders of magnitude between an empty board and a sampler with a library
// loaded, and a log scale is the only one that shows movement across all
// of it.
//
// The warnings are absolute shares of RAM, not positions on the track: one
// process holding half the machine deserves amber whatever the track looks
// like. The tick is drawn at the amber share's own position so the tick and
// the colour change in the same place.
constexpr double kMemoryFloorBytes = 64.0 * 1024.0 * 1024.0;
constexpr float kMemoryWarnShare = 0.50f;
constexpr float kMemoryCriticalShare = 0.75f;

constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr double kBytesPerGigabyte = kBytesPerMegabyte * 1024.0;
// Below this, show megabytes. A host sitting at "0.4G" tells you less than
// one sitting at "412M", and the switch happens where the megabyte figure
// stops fitting the column.
constexpr double kGigabyteThreshold = kBytesPerGigabyte;

const juce::String& emDash()
{
    static const juce::String dash(juce::CharPointer_UTF8("\xe2\x80\x94"));
    return dash;
}

juce::Colour barColour(meter::Zone zone)
{
    switch (zone)
    {
    case meter::Zone::Critical:
        return tokens::accentDestructive;
    case meter::Zone::Warning:
        return tokens::accentWarning;
    case meter::Zone::Normal:
        break;
    }
    return tokens::accentPrimary;
}

juce::Colour numberColour(meter::Zone zone)
{
    return zone == meter::Zone::Normal ? tokens::fgPrimary : barColour(zone);
}

int percent(float load)
{
    return juce::roundToInt(juce::jlimit(0.0f, 1.0f, load) * 100.0f);
}

// `bytes` on a log track running from kMemoryFloorBytes to `total`.
float memoryTrackPosition(std::uint64_t bytes, std::uint64_t total)
{
    return meter::logPosition(static_cast<double>(bytes), kMemoryFloorBytes, static_cast<double>(total));
}

meter::Zone memoryZone(float share)
{
    if (share >= kMemoryCriticalShare)
        return meter::Zone::Critical;
    if (share >= kMemoryWarnShare)
        return meter::Zone::Warning;
    return meter::Zone::Normal;
}

juce::String shortBytes(std::uint64_t bytes)
{
    const auto b = static_cast<double>(bytes);
    if (b >= kGigabyteThreshold)
        return juce::String(b / kBytesPerGigabyte, 1) + "G";
    return juce::String(juce::roundToInt(b / kBytesPerMegabyte)) + "M";
}

} // namespace

SystemMeters::SystemMeters(juce::AudioDeviceManager* deviceManager) : deviceManager_(deviceManager)
{
    setInterceptsMouseClicks(true, false); // for the tooltip

    totalBytes_ = static_cast<std::uint64_t>(juce::SystemStats::getMemorySizeInMegabytes()) *
                  static_cast<std::uint64_t>(kBytesPerMegabyte);
    refreshMemory();
    startTimerHz(kRefreshHz);
}

SystemMeters::~SystemMeters()
{
    stopTimer();
}

void SystemMeters::refreshMemory()
{
    memoryBytes_ = platform::processMemoryBytes();
    memoryFraction_ = totalBytes_ > 0
                          ? static_cast<float>(static_cast<double>(memoryBytes_) / static_cast<double>(totalBytes_))
                          : 0.0f;
}

void SystemMeters::refreshTooltip()
{
    juce::String tip;
    if (hasDevice_)
    {
        tip << "Audio CPU load " << percent(shown_) << "% (peak " << percent(peak_.peak())
            << "%). Amber from 70%, red from 90%.";
        if (auto* device = deviceManager_->getCurrentAudioDevice())
            if (const int xruns = device->getXRunCount(); xruns >= 0)
                tip << "\nDropouts since the device started: " << xruns;
    }
    else
    {
        tip << "Audio CPU load: no audio device";
    }

    tip << "\n";
    if (memoryBytes_ == 0)
        tip << "Memory: this platform does not report it";
    else if (totalBytes_ > 0)
        tip << "Memory: Pedalboard 3 is holding " << shortBytes(memoryBytes_) << " of " << shortBytes(totalBytes_)
            << " RAM (" << percent(memoryFraction_) << "%). Amber past half the machine, red past three quarters.";
    else
        tip << "Memory: Pedalboard 3 is holding " << shortBytes(memoryBytes_);

    setTooltip(tip);
}

void SystemMeters::timerCallback()
{
    auto* device = deviceManager_ != nullptr ? deviceManager_->getCurrentAudioDevice() : nullptr;
    hasDevice_ = device != nullptr;

    if (hasDevice_)
    {
        const auto load = static_cast<float>(juce::jlimit(0.0, 1.0, deviceManager_->getCpuUsage()));
        shown_ = meter::smooth(shown_, load, 0.35f);
        peak_.update(load, juce::Time::getMillisecondCounterHiRes());
    }
    else
    {
        shown_ = 0.0f;
    }

    if (--ticksUntilMemory_ <= 0)
    {
        ticksUntilMemory_ = kMemoryEveryTicks;
        refreshMemory();
    }

    refreshTooltip();
    repaint();
}

void SystemMeters::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().withSizeKeepingCentre(getWidth(), meter::kStackHeight);

    const auto cpuZone = meter::zoneFor(shown_);
    meter::Row cpu;
    cpu.caption = "CPU";
    cpu.fill = shown_;
    cpu.peak = peak_.peak();
    cpu.tick = meter::kWarningLoad;
    cpu.bar = barColour(cpuZone);
    cpu.number = hasDevice_ ? juce::String(percent(shown_)) + "%" : emDash();
    cpu.numberText = numberColour(cpuZone);
    cpu.active = hasDevice_;

    const auto memZone = memoryZone(memoryFraction_);
    meter::Row mem;
    mem.caption = "MEM";
    mem.fill = memoryTrackPosition(memoryBytes_, totalBytes_);
    mem.tick = memoryTrackPosition(static_cast<std::uint64_t>(static_cast<double>(totalBytes_) * kMemoryWarnShare),
                                   totalBytes_);
    mem.bar = barColour(memZone);
    mem.number = memoryBytes_ > 0 ? shortBytes(memoryBytes_) : emDash();
    mem.numberText = numberColour(memZone);
    mem.active = memoryBytes_ > 0;

    meter::drawRow(g, area.removeFromTop(meter::kRowHeight), kCaptionWidth, kNumberWidth, cpu);
    area.removeFromTop(meter::kRowGap);
    meter::drawRow(g, area.removeFromTop(meter::kRowHeight), kCaptionWidth, kNumberWidth, mem);
}

} // namespace pedalboard3::ui
