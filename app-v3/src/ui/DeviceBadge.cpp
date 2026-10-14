#include "DeviceBadge.h"

#include "ColourScheme.h"
#include "engine/AudioEngine.h"
#include "Fonts.h"
#include "Icons.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace pedalboard3::ui
{

namespace
{
constexpr int kChipPadding = 7;    // inside the driver chip
constexpr int kChipGap = 5;        // between the driver and the rate
constexpr int kGroupGap = 12;      // between the chip and a device, and between devices
constexpr int kIconSize = 13;      // the mic and speaker
constexpr int kIconTextGap = 5;    // icon to name
constexpr int kNameMaxWidth = 150; // a device name past this is ellipsised
constexpr float kDriverSize = 12.0f;
constexpr float kRateSize = 10.5f;
constexpr float kNameSize = 12.0f;

juce::Font driverFont()
{
    return fonts::ui(kDriverSize, fonts::Weight::SemiBold);
}
juce::Font rateFont()
{
    return fonts::mono(kRateSize);
}
juce::Font nameFont()
{
    return fonts::ui(kNameSize);
}

// "48kHz", "44.1kHz" — the demo's spelling, with the decimal only where the
// rate actually has one.
juce::String formatRate(double hz)
{
    if (hz <= 0.0)
        return {};
    const double k = hz / 1000.0;
    const bool whole = std::abs(k - std::round(k)) < 0.001;
    return (whole ? juce::String(static_cast<int>(std::round(k))) : juce::String(k, 1)) + "kHz";
}

int widthOf(const juce::Font& font, const juce::String& text, int cap)
{
    return juce::jmin(cap, juce::GlyphArrangement::getStringWidthInt(font, text));
}
} // namespace

DeviceBadge::DeviceBadge(AppContext& ctx) : juce::Button("Audio device"), ctx_(ctx)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    if (auto* dm = ctx_.audioEngine.getDeviceManager())
        dm->addChangeListener(this);
    refresh();
}

DeviceBadge::~DeviceBadge()
{
    if (auto* dm = ctx_.audioEngine.getDeviceManager())
        dm->removeChangeListener(this);
}

void DeviceBadge::setFoldLevel(int level)
{
    const auto clamped = juce::jlimit(0, 2, level);
    if (clamped == foldLevel_)
        return;
    foldLevel_ = clamped;
    measure();
    repaint();
}

void DeviceBadge::refresh()
{
    auto* dm = ctx_.audioEngine.getDeviceManager();
    auto* dev = dm != nullptr ? dm->getCurrentAudioDevice() : nullptr;

    driver_ = dm != nullptr ? dm->getCurrentAudioDeviceType() : juce::String();
    if (driver_.isEmpty())
        driver_ = "Audio";

    if (dev == nullptr)
    {
        rate_.clear();
        inName_.clear();
        outName_.clear();
        // The same reason the status bar used to give, kept short enough for
        // the bar; the tooltip carries the whole of it.
        const auto& err = ctx_.audioEngine.getLastInitError();
        problem_ = err.isNotEmpty() ? "Device error" : "No device";
        setTooltip(dm == nullptr ? juce::String("There is no audio device manager.")
                   : err.isNotEmpty()
                       ? "Couldn't open the audio device: " + err + "\nClick to open Settings > Audio Devices."
                       : juce::String("No audio device is open.\nClick to open Settings > Audio Devices."));
    }
    else
    {
        problem_.clear();
        rate_ = formatRate(dev->getCurrentSampleRate());

        const auto setup = dm->getAudioDeviceSetup();
        inName_ = setup.inputDeviceName;
        outName_ = setup.outputDeviceName;
        if (outName_.isEmpty())
            outName_ = dev->getName();

        // One device doing both — an ASIO driver, usually. Printing the same
        // name twice is the clutter this issue is about, so it is shown once
        // under the speaker and the tooltip says it is also the input.
        const bool shared = inName_ == outName_;
        if (shared)
            inName_.clear();

        const int buffer = dev->getCurrentBufferSizeSamples();
        const double hz = dev->getCurrentSampleRate();
        juce::String tip = driver_ + "\n";
        if (shared)
            tip << "In and out: " << outName_ << "\n";
        else
            tip << "In: " << (setup.inputDeviceName.isEmpty() ? juce::String("none") : setup.inputDeviceName) << "\n"
                << "Out: " << outName_ << "\n";
        tip << juce::String(static_cast<int>(hz)) << " Hz, " << buffer << " samples";
        if (hz > 0.0)
            tip << " (" << juce::String(buffer / hz * 2000.0, 1) << " ms round trip)";
        tip << "\nClick to open Settings > Audio Devices.";
        setTooltip(tip);
    }

    measure();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void DeviceBadge::measure()
{
    const auto chipText = problem_.isNotEmpty() ? problem_ : rate_;
    chipWidth_ = kChipPadding * 2 + juce::GlyphArrangement::getStringWidthInt(driverFont(), driver_);
    if (chipText.isNotEmpty())
        chipWidth_ += kChipGap + juce::GlyphArrangement::getStringWidthInt(
                                     problem_.isNotEmpty() ? nameFont() : rateFont(), chipText);

    const auto deviceWidth = [](const juce::String& name)
    { return name.isEmpty() ? 0 : kIconSize + kIconTextGap + widthOf(nameFont(), name, kNameMaxWidth); };

    inWidth_ = foldLevel_ <= 1 ? deviceWidth(inName_) : 0;
    outWidth_ = foldLevel_ == 0 ? deviceWidth(outName_) : 0;

    preferredWidth_ = chipWidth_;
    if (inWidth_ > 0)
        preferredWidth_ += kGroupGap + inWidth_;
    if (outWidth_ > 0)
        preferredWidth_ += kGroupGap + outWidth_;
}

void DeviceBadge::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto area = getLocalBounds();

    // The whole badge lights on hover, so it reads as the way in to Audio
    // Devices rather than as a label that happens to be clickable.
    if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
    {
        g.setColour(tokens::bgSecondary.withAlpha(shouldDrawButtonAsDown ? 0.9f : 0.55f));
        g.fillRoundedRectangle(area.toFloat(), 4.0f);
    }

    const bool bad = problem_.isNotEmpty();

    // The driver chip.
    auto chip = area.removeFromLeft(chipWidth_).reduced(0, 4);
    g.setColour(tokens::bgMuted);
    g.fillRoundedRectangle(chip.toFloat(), 3.0f);
    auto chipInner = chip.reduced(kChipPadding, 0);
    g.setFont(driverFont());
    g.setColour(bad ? tokens::accentWarning : tokens::accentPrimary);
    const int driverWidth = juce::GlyphArrangement::getStringWidthInt(driverFont(), driver_);
    g.drawText(driver_, chipInner.removeFromLeft(driverWidth), juce::Justification::centredLeft);
    const auto chipText = bad ? problem_ : rate_;
    if (chipText.isNotEmpty())
    {
        chipInner.removeFromLeft(kChipGap);
        g.setFont(bad ? nameFont() : rateFont());
        g.setColour(bad ? tokens::accentWarning : tokens::fgMuted);
        g.drawText(chipText, chipInner, juce::Justification::centredLeft);
    }

    if (bad)
        return;

    const auto drawDevice = [&](juce::Rectangle<int>& row, const juce::Path& icon, const juce::String& name, int width)
    {
        if (width <= 0)
            return;
        row.removeFromLeft(kGroupGap);
        auto cell = row.removeFromLeft(width);
        g.setColour(tokens::fgMuted);
        strokeIcon(g, icon, cell.removeFromLeft(kIconSize).toFloat());
        cell.removeFromLeft(kIconTextGap);
        g.setFont(nameFont());
        g.drawText(name, cell, juce::Justification::centredLeft, true);
    };

    drawDevice(area, micIcon(), inName_, inWidth_);
    drawDevice(area, speakerIcon(), outName_, outWidth_);
}

} // namespace pedalboard3::ui
