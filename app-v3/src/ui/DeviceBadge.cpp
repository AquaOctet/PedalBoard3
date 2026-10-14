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
juce::Font channelFont()
{
    return fonts::mono(kRateSize);
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

// JUCE's device-type names are what Windows calls the APIs, which is not what
// anyone calls them. The demo's chip says "ASIO" and "WASAPI", so say that.
// Anything unrecognised is passed through rather than guessed at.
juce::String shortDriver(const juce::String& type)
{
    if (type == "Windows Audio")
        return "WASAPI";
    if (type == "Windows Audio (Exclusive Mode)")
        return "WASAPI EX";
    if (type == "Windows Audio (Low Latency Mode)")
        return "WASAPI LL";
    return type;
}

// Windows device names carry two things the badge shows separately anyway:
// the channel pair ("Output 1/2 ...") and the enumerator Windows adds when
// there are two of a card ("(2- Komplete Audio 6 MK2)"). Strip both, then
// pick whichever half actually names the hardware:
//
//   "Output 1/2 (2- Komplete Audio 6 MK2)"          -> "Komplete Audio 6 MK2"
//   "Speakers (Realtek(R) Audio)"                   -> "Realtek(R) Audio"
//   "Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)" -> "Voicemeeter Out B1"
//   "Focusrite USB ASIO"                            -> unchanged
//
// The third case is why this is not just "take the parenthetical": B1 and B2
// are different outputs of one driver, and the parenthetical is identical for
// both. A prefix is only dropped when it is a generic Windows endpoint word.
juce::String shortDeviceName(juce::String name)
{
    name = name.trim();
    if (name.isEmpty())
        return name;

    const int open = name.indexOfChar('(');
    const int close = name.lastIndexOfChar(')');
    if (open < 0 || close < open)
        return name;

    auto prefix = name.substring(0, open).trim();
    auto inner = name.substring(open + 1, close).trim();

    // "2- Komplete Audio 6 MK2" -> "Komplete Audio 6 MK2"
    if (inner.length() > 3 && juce::CharacterFunctions::isDigit(inner[0]) && inner.substring(1, 3) == "- ")
        inner = inner.substring(3).trim();

    // A prefix that is only a channel spec, or only a Windows endpoint word,
    // names nothing; the hardware is in the brackets.
    static const char* generic[] = {
        "Speakers", "Headphones", "Headset", "Microphone", "Line In", "Line Out", "Digital Audio", "Realtek"};
    bool prefixIsGeneric = prefix.isEmpty();
    for (const char* g : generic)
        if (prefix.equalsIgnoreCase(g) || prefix.startsWithIgnoreCase(juce::String(g) + " "))
            prefixIsGeneric = true;
    if (prefix.startsWithIgnoreCase("Input ") || prefix.startsWithIgnoreCase("Output "))
        prefixIsGeneric = true;

    return prefixIsGeneric ? inner : prefix;
}

// "[1,2]" — the active channels, 1-based, as the demo shows them. Empty when
// nothing is active, which is a real state worth not dressing up.
juce::String channelList(const juce::BigInteger& active)
{
    juce::StringArray parts;
    for (int i = 0; i <= active.getHighestBit(); ++i)
        if (active[i])
            parts.add(juce::String(i + 1));
    return parts.isEmpty() ? juce::String() : "[" + parts.joinIntoString(",") + "]";
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

    driver_ = dm != nullptr ? shortDriver(dm->getCurrentAudioDeviceType()) : juce::String();
    if (driver_.isEmpty())
        driver_ = "Audio";

    if (dev == nullptr)
    {
        rate_.clear();
        inName_.clear();
        outName_.clear();
        inChannels_.clear();
        outChannels_.clear();
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
        const auto rawIn = setup.inputDeviceName;
        const auto rawOut = setup.outputDeviceName.isEmpty() ? dev->getName() : setup.outputDeviceName;
        inName_ = shortDeviceName(rawIn);
        outName_ = shortDeviceName(rawOut);
        inChannels_ = channelList(dev->getActiveInputChannels());
        outChannels_ = channelList(dev->getActiveOutputChannels());

        // Both ends are shown even when one device serves both, which is how
        // the demo does it. With the channel pair beside each, the two lines
        // say different things — in on [1,2], out on [3,4] is a real and
        // common setup — so this is no longer the duplication it looks like.
        const int buffer = dev->getCurrentBufferSizeSamples();
        const double hz = dev->getCurrentSampleRate();
        juce::String tip = driver_ + "\n";
        tip << "In: " << (rawIn.isEmpty() ? juce::String("none") : rawIn) << " " << inChannels_ << "\n"
            << "Out: " << rawOut << " " << outChannels_ << "\n";
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

    const auto deviceWidth = [](const juce::String& name, const juce::String& channels)
    {
        if (name.isEmpty())
            return 0;
        int w = kIconSize + kIconTextGap + widthOf(nameFont(), name, kNameMaxWidth);
        if (channels.isNotEmpty())
            w += kChipGap + juce::GlyphArrangement::getStringWidthInt(channelFont(), channels);
        return w;
    };

    inWidth_ = foldLevel_ <= 1 ? deviceWidth(inName_, inChannels_) : 0;
    outWidth_ = foldLevel_ == 0 ? deviceWidth(outName_, outChannels_) : 0;

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

    const auto drawDevice = [&](juce::Rectangle<int>& row,
                                const juce::Path& icon,
                                const juce::String& name,
                                const juce::String& channels,
                                int width)
    {
        if (width <= 0)
            return;
        row.removeFromLeft(kGroupGap);
        auto cell = row.removeFromLeft(width);
        g.setColour(tokens::fgMuted);
        strokeIcon(g, icon, cell.removeFromLeft(kIconSize).toFloat());
        cell.removeFromLeft(kIconTextGap);

        // The channel pair is pulled off the right first, so a long name
        // ellipsises into the space left rather than pushing the pair out of
        // the cell. It takes the accent, as it does in the demo: it is the
        // part that changes when you re-patch.
        if (channels.isNotEmpty())
        {
            const int w = juce::GlyphArrangement::getStringWidthInt(channelFont(), channels);
            auto chans = cell.removeFromRight(w);
            cell.removeFromRight(kChipGap);
            g.setFont(channelFont());
            g.setColour(tokens::accentPrimary);
            g.drawText(channels, chans, juce::Justification::centredLeft);
        }

        g.setFont(nameFont());
        g.setColour(tokens::fgMuted);
        g.drawText(name, cell, juce::Justification::centredLeft, true);
    };

    drawDevice(area, micIcon(), inName_, inChannels_, inWidth_);
    drawDevice(area, speakerIcon(), outName_, outChannels_, outWidth_);
}

} // namespace pedalboard3::ui
