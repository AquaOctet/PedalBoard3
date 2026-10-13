#include "AboutWindow.h"

#include "ColourScheme.h"
#include "DialogFrame.h"
#include "Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace pedalboard3::ui
{

namespace
{

// Where the project and its people are, in one place. The project is kept on
// GitLab and mirrored to GitHub, the public one, so issues and releases link
// there.
constexpr const char* kPedalboard2Url = "https://www.niallmoody.com/work/pedalboard2/";
constexpr const char* kAquaOctetUrl = "https://aquaoctet.com";
constexpr const char* kAquaOctetGitHubUrl = "https://github.com/AquaOctet";
constexpr const char* kOpenZephyrUrl = "https://openzephyr.com";
constexpr const char* kGitLabUrl = "https://gitlab.com/void-realm-solutions/pedalboard-3";
constexpr const char* kGitHubUrl = "https://github.com/AquaOctet/PedalBoard3";
constexpr const char* kIssuesUrl = "https://github.com/AquaOctet/PedalBoard3/issues";
constexpr const char* kReleasesUrl = "https://github.com/AquaOctet/PedalBoard3/releases";

constexpr int kWidth = 540;
constexpr int kMarginX = 26;
constexpr int kMarginY = 22;
constexpr int kTitleHeight = 40;
constexpr int kTitleGap = 10;
constexpr int kLineHeight = 22;
constexpr int kParagraphGap = 12;
constexpr int kCloseGap = 18;
constexpr int kButtonWidth = 80;
constexpr int kButtonHeight = 30;

enum class Style
{
    Text,
    Mono,
    Muted
};

// A run of words on a line: plain text, or a link when it has a URL.
struct Piece
{
    juce::String text;
    juce::String url;
    Style style = Style::Text;
};

// The mockup's About (UI chrome U4): the name in the display face with a teal
// "3", then who made it, where it lives, and the version.
class AboutPanel : public juce::Component
{
public:
    AboutPanel()
    {
        const juce::String dot(juce::CharPointer_UTF8(" \xc2\xb7 "));
        addLine({{"The revamp of "}, {"Pedalboard2", kPedalboard2Url}, {" by Niall Moody,"}});
        addLine({{"recompiled with love and a lot of tokens by aquaOctet at OpenZephyr LLC."}});
        addLine({{"aquaoctet.com", kAquaOctetUrl},
                 {dot},
                 {"github.com/AquaOctet", kAquaOctetGitHubUrl},
                 {dot},
                 {"openzephyr.com", kOpenZephyrUrl}});
        paragraph();
        addLine({{"Maintained on "},
                 {"GitLab", kGitLabUrl},
                 {", mirrored to "},
                 {"GitHub", kGitHubUrl},
                 {" (GitLab is better at CI/CD)."}});
        addLine({{"Found a bug or have an idea? Feel free to "}, {"open an issue", kIssuesUrl}, {"."}});
        paragraph();
        addLine({{"Version " JUCE_APPLICATION_VERSION_STRING, {}, Style::Mono},
                 {dot},
                 {"Check for updates", kReleasesUrl}});
        addLine({{"Copyright 2026 OpenZephyr LLC", {}, Style::Muted}, {dot}, {"Built with JUCE.", {}, Style::Muted}});

        closeButton_.onClick = [this]
        {
            if (auto* frame = findParentComponentOfClass<DialogFrame>())
                frame->closeButtonPressed();
        };
        addAndMakeVisible(closeButton_);
        setSize(kWidth, contentHeight());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(tokens::bgWindow);
        auto area = getLocalBounds().reduced(kMarginX, kMarginY);

        const auto title = fonts::display(34.0f);
        juce::AttributedString name;
        name.append("Pedalboard ", title, tokens::fgPrimary);
        name.append("3", title, tokens::accentPrimary);
        name.draw(g, area.removeFromTop(kTitleHeight).toFloat());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(kMarginX, kMarginY);
        area.removeFromTop(kTitleHeight + kTitleGap);
        for (const auto& row : rows_)
        {
            area.removeFromTop(row.gapBefore);
            const auto line = area.removeFromTop(kLineHeight);
            for (const auto& placed : row.pieces)
                placed.component->setBounds(line.getX() + placed.x, line.getY(), placed.width, line.getHeight());
        }
        closeButton_.setBounds(
            getWidth() - kMarginX - kButtonWidth, getHeight() - kMarginY - kButtonHeight, kButtonWidth, kButtonHeight);
    }

private:
    struct Placed
    {
        juce::Component* component;
        int x; // from the line's left edge
        int width;
    };

    struct Row
    {
        int gapBefore = 0;
        std::vector<Placed> pieces;
    };

    void paragraph() { pendingGap_ = kParagraphGap; }

    // Each piece's text starts where it would in one continuous line, so the
    // spaces between words and links are the font's own. A label draws its
    // text trimmed and flush left; a link draws its text 1 px in.
    void addLine(std::initializer_list<Piece> pieces)
    {
        Row row;
        row.gapBefore = std::exchange(pendingGap_, 0);
        float x = 0.0f;
        for (const auto& piece : pieces)
        {
            const auto font = piece.style == Style::Mono ? fonts::mono(12.0f) : fonts::ui(14.0f);
            const auto shown = piece.text.trim();
            const int lead = piece.text.length() - piece.text.trimStart().length();
            const int trail = piece.text.length() - piece.text.trimEnd().length();
            const float textX = x + spacesWidth(font, lead);
            const float textWidth = juce::GlyphArrangement::getStringWidth(font, shown);
            x = textX + textWidth + spacesWidth(font, trail);

            const bool isLink = piece.url.isNotEmpty();
            std::unique_ptr<juce::Component> component;
            if (isLink)
            {
                auto link = std::make_unique<juce::HyperlinkButton>(shown, juce::URL(piece.url));
                link->setFont(font, false, juce::Justification::centredLeft);
                link->setColour(juce::HyperlinkButton::textColourId, tokens::accentPrimary);
                link->setTooltip(piece.url);
                component = std::move(link);
            }
            else
            {
                auto label = std::make_unique<juce::Label>(juce::String(), shown);
                label->setFont(font);
                label->setBorderSize({});
                label->setColour(juce::Label::textColourId,
                                 piece.style == Style::Text ? tokens::fgSecondaryText : tokens::fgMuted);
                label->setInterceptsMouseClicks(false, false);
                component = std::move(label);
            }
            addAndMakeVisible(*component);
            const int width = static_cast<int>(std::ceil(textWidth));
            row.pieces.push_back(
                {component.get(), juce::roundToInt(textX) - (isLink ? 1 : 0), width + (isLink ? 3 : 1)});
            owned_.push_back(std::move(component));
        }
        rows_.push_back(std::move(row));
    }

    // What `count` spaces add between two words in `font`.
    static float spacesWidth(const juce::Font& font, int count)
    {
        if (count <= 0)
            return 0.0f;
        return juce::GlyphArrangement::getStringWidth(font, "a" + juce::String::repeatedString(" ", count) + "a") -
               juce::GlyphArrangement::getStringWidth(font, "aa");
    }

    int contentHeight() const
    {
        int height = kMarginY + kTitleHeight + kTitleGap;
        for (const auto& row : rows_)
            height += row.gapBefore + kLineHeight;
        return height + kCloseGap + kButtonHeight + kMarginY;
    }

    std::vector<std::unique_ptr<juce::Component>> owned_;
    std::vector<Row> rows_;
    int pendingGap_ = 0;
    juce::TextButton closeButton_{"Close"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};

} // namespace

std::unique_ptr<juce::Component> makeAboutPanel()
{
    return std::make_unique<AboutPanel>();
}

void showAboutWindow()
{
    DialogFrame::launchModal("About Pedalboard 3", makeAboutPanel(), false);
}

} // namespace pedalboard3::ui
