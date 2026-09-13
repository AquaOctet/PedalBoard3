#include "Fonts.h"

#include "Pb3Fonts.h" // juce_add_binary_data(pb3_ui_fonts), namespace pb3fonts

namespace pedalboard3::ui::fonts
{

namespace
{

// The mockup's caption tracking (letter-spacing: 0.12em).
constexpr float kCaptionKerning = 0.12f;

juce::Typeface::Ptr load(const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
}

// Loaded once, on first use (message thread).
struct Faces
{
    juce::Typeface::Ptr uiRegular =
        load(pb3fonts::BarlowSemiCondensedRegular_ttf, pb3fonts::BarlowSemiCondensedRegular_ttfSize);
    juce::Typeface::Ptr uiMedium =
        load(pb3fonts::BarlowSemiCondensedMedium_ttf, pb3fonts::BarlowSemiCondensedMedium_ttfSize);
    juce::Typeface::Ptr uiSemiBold =
        load(pb3fonts::BarlowSemiCondensedSemiBold_ttf, pb3fonts::BarlowSemiCondensedSemiBold_ttfSize);
    juce::Typeface::Ptr uiBold = load(pb3fonts::BarlowSemiCondensedBold_ttf, pb3fonts::BarlowSemiCondensedBold_ttfSize);
    juce::Typeface::Ptr captionMedium =
        load(pb3fonts::BarlowCondensedMedium_ttf, pb3fonts::BarlowCondensedMedium_ttfSize);
    juce::Typeface::Ptr captionSemiBold =
        load(pb3fonts::BarlowCondensedSemiBold_ttf, pb3fonts::BarlowCondensedSemiBold_ttfSize);
    juce::Typeface::Ptr captionBold = load(pb3fonts::BarlowCondensedBold_ttf, pb3fonts::BarlowCondensedBold_ttfSize);
    juce::Typeface::Ptr monoRegular = load(pb3fonts::JetBrainsMonoRegular_ttf, pb3fonts::JetBrainsMonoRegular_ttfSize);
    juce::Typeface::Ptr monoMedium = load(pb3fonts::JetBrainsMonoMedium_ttf, pb3fonts::JetBrainsMonoMedium_ttfSize);
};

const Faces& faces()
{
    static const Faces instance;
    return instance;
}

juce::Font make(juce::Typeface::Ptr typeface, float pointHeight, float kerning = 0.0f)
{
    return juce::Font(juce::FontOptions(std::move(typeface)).withPointHeight(pointHeight).withKerningFactor(kerning));
}

} // namespace

juce::Typeface::Ptr uiTypeface(Weight weight)
{
    const auto& f = faces();
    switch (weight)
    {
    case Weight::Regular:
        return f.uiRegular;
    case Weight::Medium:
        return f.uiMedium;
    case Weight::SemiBold:
        return f.uiSemiBold;
    case Weight::Bold:
        return f.uiBold;
    }
    return f.uiRegular;
}

juce::Typeface::Ptr captionTypeface(Weight weight)
{
    const auto& f = faces();
    switch (weight)
    {
    case Weight::Regular:
    case Weight::Medium:
        return f.captionMedium;
    case Weight::SemiBold:
        return f.captionSemiBold;
    case Weight::Bold:
        return f.captionBold;
    }
    return f.captionSemiBold;
}

juce::Typeface::Ptr monoTypeface(Weight weight)
{
    const auto& f = faces();
    return weight == Weight::Regular ? f.monoRegular : f.monoMedium;
}

juce::Font ui(float pointHeight, Weight weight)
{
    return make(uiTypeface(weight), pointHeight);
}

juce::Font caption(float pointHeight, Weight weight)
{
    return make(captionTypeface(weight), pointHeight, kCaptionKerning);
}

juce::Font display(float pointHeight)
{
    return make(captionTypeface(Weight::Bold), pointHeight);
}

juce::Font mono(float pointHeight, Weight weight)
{
    return make(monoTypeface(weight), pointHeight);
}

} // namespace pedalboard3::ui::fonts
