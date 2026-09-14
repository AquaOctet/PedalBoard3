// Tests for ui::fonts (ui/Fonts.h) and the LookAndFeel's font mapping (UI
// chrome plan docs/plans/2026-09-13-002-feat-ui-chrome-plan.md, U2 / KD1): the
// mockup's typefaces are embedded, load with their families, and stand in for
// JUCE's default sans-serif and monospaced fonts.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/Fonts.h"
#include "ui/Pb3LookAndFeel.h"

using namespace pedalboard3::ui;
using Catch::Approx;

namespace
{

// A face's family and style together. Where the width goes differs by
// platform: DirectWrite (Windows) names Barlow Semi Condensed Bold "Barlow" /
// "Semi Condensed Bold", while FreeType and CoreText keep "Barlow Semi
// Condensed" as the family.
juce::String fullName(const juce::Typeface::Ptr& typeface)
{
    return typeface->getName() + " " + typeface->getStyle();
}

bool isUiFace(const juce::Typeface::Ptr& typeface)
{
    const auto name = fullName(typeface);
    return name.startsWith("Barlow") && name.contains("Semi Condensed");
}

bool isCaptionFace(const juce::Typeface::Ptr& typeface)
{
    const auto name = fullName(typeface);
    return name.startsWith("Barlow") && name.contains("Condensed") && !name.contains("Semi Condensed");
}

bool isMonoFace(const juce::Typeface::Ptr& typeface)
{
    return fullName(typeface).startsWith("JetBrains Mono");
}

} // namespace

TEST_CASE("Fonts: the embedded typefaces load, with the mockup's families", "[ui][fonts]")
{
    for (const auto weight :
         {fonts::Weight::Regular, fonts::Weight::Medium, fonts::Weight::SemiBold, fonts::Weight::Bold})
    {
        const auto ui = fonts::uiTypeface(weight);
        REQUIRE(ui != nullptr);
        const auto caption = fonts::captionTypeface(weight);
        REQUIRE(caption != nullptr);
        const auto mono = fonts::monoTypeface(weight);
        REQUIRE(mono != nullptr);
        CAPTURE(fullName(ui), fullName(caption), fullName(mono));

        CHECK(isUiFace(ui));
        CHECK(isCaptionFace(caption));
        CHECK(isMonoFace(mono));
    }

    // Each weight is its own face.
    REQUIRE(fonts::uiTypeface(fonts::Weight::Regular) != fonts::uiTypeface(fonts::Weight::Bold));
    REQUIRE(fonts::monoTypeface(fonts::Weight::Regular) != fonts::monoTypeface(fonts::Weight::Medium));
}

TEST_CASE("Fonts: mono has equal digit widths, and the UI face is proportional", "[ui][fonts]")
{
    const auto mono = fonts::mono(12.0f);
    REQUIRE(juce::GlyphArrangement::getStringWidth(mono, "1111") ==
            Approx(juce::GlyphArrangement::getStringWidth(mono, "8888")));

    const auto ui = fonts::ui(14.0f);
    REQUIRE(juce::GlyphArrangement::getStringWidth(ui, "iiii") < juce::GlyphArrangement::getStringWidth(ui, "MMMM"));
}

TEST_CASE("Fonts: sizes are point heights, as the mockup's CSS", "[ui][fonts]")
{
    // A larger point height draws larger text, and the caption face carries the
    // mockup's letter-spacing.
    REQUIRE(fonts::ui(14.0f).getHeight() > fonts::ui(12.0f).getHeight());
    REQUIRE(fonts::caption(12.0f).getExtraKerningFactor() == Approx(0.12f));
    REQUIRE(fonts::ui(12.0f).getExtraKerningFactor() == Approx(0.0f));
}

TEST_CASE("Fonts: the LookAndFeel maps JUCE's default fonts onto the embedded ones", "[ui][fonts]")
{
    Pb3LookAndFeel lookAndFeel;

    const auto sans = lookAndFeel.getTypefaceForFont(juce::Font(juce::FontOptions(14.0f)));
    REQUIRE(sans != nullptr);
    CAPTURE(fullName(sans));
    REQUIRE(isUiFace(sans));

    // Bold is Barlow's SemiBold, as the mockup's headings.
    REQUIRE(lookAndFeel.getTypefaceForFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold"))) ==
            fonts::uiTypeface(fonts::Weight::SemiBold));

    const auto mono = lookAndFeel.getTypefaceForFont(
        juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, 0)));
    REQUIRE(mono != nullptr);
    REQUIRE(isMonoFace(mono));
}
