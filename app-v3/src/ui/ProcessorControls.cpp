#include "ProcessorControls.h"

#include "ColourScheme.h"
#include "engine/processors/AudioRecorderProcessor.h"
#include "engine/processors/FilePlayerProcessor.h"
#include "FilePlayerWindow.h"
#include "Icons.h"
#include "RecorderWindow.h"
#include "session/BankDocument.h"
#include "session/Session.h"

#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace pedalboard3::ui
{

ProcessorControls::ProcessorControls(AppContext& ctx, session::NodeId node, int refreshHz) : ctx_(ctx), node_(node)
{
    startTimerHz(refreshHz);
}

ProcessorControls::~ProcessorControls()
{
    stopTimer();
}

juce::AudioProcessor* ProcessorControls::processor() const
{
    if (ctx_.session.getState() != session::Session::State::Idle)
        return nullptr;
    return ctx_.session.getHostedPlugin(node_);
}

juce::AudioProcessorParameterWithID* ProcessorControls::parameter(const juce::String& id) const
{
    if (auto* p = processor())
        for (auto* candidate : p->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(candidate);
                withId != nullptr && withId->paramID == id)
                return withId;
    return nullptr;
}

namespace
{

using engine::processors::FilePlayerProcessor;

juce::String minutesAndSeconds(double seconds)
{
    const int whole = static_cast<int>(std::floor(juce::jmax(0.0, seconds)));
    return juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
}

// ---- Output Toggle (M6 U10): one big 1 / 2 switch ------------------------------

// The lit half is where the input goes; clicking the other half switches it.
class OutputToggleControls final : public ProcessorControls
{
public:
    OutputToggleControls(AppContext& ctx, session::NodeId node) : ProcessorControls(ctx, node)
    {
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            auto& button = buttons_[i];
            button.setButtonText(i == 0 ? "1" : "2");
            button.setClickingTogglesState(false);
            button.setColour(juce::TextButton::buttonColourId, tokens::bgSecondary);
            button.setColour(juce::TextButton::buttonOnColourId, tokens::accentPrimary);
            button.setColour(juce::TextButton::textColourOnId, tokens::fgInverse);
            button.setTooltip(i == 0 ? "Send the input to output 1 (the left output port)"
                                     : "Send the input to output 2 (the right output port)");
            button.setConnectedEdges(i == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
            button.onClick = [this, i] { choose(static_cast<int>(i)); };
            addAndMakeVisible(button);
        }
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        buttons_[0].setBounds(area.removeFromLeft(area.getWidth() / 2));
        buttons_[1].setBounds(area);
    }

private:
    juce::AudioParameterChoice* output() const
    {
        return dynamic_cast<juce::AudioParameterChoice*>(parameter("output"));
    }

    void choose(int index)
    {
        if (auto* p = output())
        {
            p->beginChangeGesture();
            *p = index; // notifies the host: a mapped footswitch and this stay in step
            p->endChangeGesture();
        }
        refresh();
    }

    void refresh() override
    {
        auto* p = output();
        const int chosen = p != nullptr ? p->getIndex() : -1;
        for (std::size_t i = 0; i < buttons_.size(); ++i)
        {
            buttons_[i].setEnabled(p != nullptr);
            buttons_[i].setToggleState(static_cast<int>(i) == chosen, juce::dontSendNotification);
        }
    }

    std::array<juce::TextButton, 2> buttons_;
};

// ---- File Player (M6 U6): play, return to zero, and the waveform -----------------

// The file's overview, lit up to where it's played, with the time; a click
// seeks there.
class Waveform final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void(double)> onSeek;

    void setPeaks(std::vector<float> peaks)
    {
        peaks_ = std::move(peaks);
        repaint();
    }

    void setState(double fraction, const juce::String& text)
    {
        if (std::abs(fraction - fraction_) < 0.0005 && text == text_)
            return;
        fraction_ = fraction;
        text_ = text;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour(tokens::bgMuted);
        g.fillRoundedRectangle(area, 3.0f);

        if (!peaks_.empty())
        {
            const float mid = area.getCentreY();
            const float half = area.getHeight() * 0.5f - 2.0f;
            const float step = area.getWidth() / static_cast<float>(peaks_.size());
            const float playedTo =
                area.getX() + area.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, fraction_));
            for (std::size_t i = 0; i < peaks_.size(); ++i)
            {
                const float x = area.getX() + step * static_cast<float>(i);
                const float h = juce::jmax(0.5f, peaks_[i] * half);
                g.setColour(x < playedTo ? tokens::accentPrimary : tokens::fgMuted.withAlpha(0.55f));
                g.fillRect(x, mid - h, juce::jmax(1.0f, step - 0.5f), h * 2.0f);
            }
            g.setColour(tokens::fgPrimary);
            g.drawVerticalLine(juce::roundToInt(playedTo), area.getY(), area.getBottom());
        }

        g.setColour(peaks_.empty() ? tokens::fgMuted : tokens::fgPrimary);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)));
        g.drawText(text_,
                   getLocalBounds().reduced(4, 1),
                   peaks_.empty() ? juce::Justification::centred : juce::Justification::topRight,
                   true);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!peaks_.empty() && onSeek && getWidth() > 0)
            onSeek(static_cast<double>(e.position.x) / static_cast<double>(getWidth()));
    }

private:
    std::vector<float> peaks_;
    double fraction_{-1.0};
    juce::String text_;
};

class FilePlayerControls final : public ProcessorControls
{
public:
    FilePlayerControls(AppContext& ctx, session::NodeId node) : ProcessorControls(ctx, node, 30)
    {
        playButton_.setShape(playIcon(), false, true, false);
        playButton_.setTooltip("Play / Pause");
        playButton_.onClick = [this]
        {
            if (auto* p = player())
                p->togglePlay();
            refresh();
        };
        returnButton_.setShape(returnToZeroIcon(), false, true, false);
        returnButton_.setTooltip("Return to Zero");
        returnButton_.onClick = [this]
        {
            if (auto* p = player())
                p->returnToZero();
            refresh();
        };
        waveform_.setTooltip("Click to jump there. ED chooses the file, Loop and Sync.");
        waveform_.onSeek = [this](double fraction)
        {
            if (auto* p = player())
                p->seekTo(fraction);
        };
        for (auto* c : std::initializer_list<juce::Component*>{&playButton_, &returnButton_, &waveform_})
            addAndMakeVisible(c);
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        playButton_.setBounds(area.removeFromLeft(22).withSizeKeepingCentre(20, 18));
        area.removeFromLeft(2);
        returnButton_.setBounds(area.removeFromLeft(22).withSizeKeepingCentre(20, 18));
        area.removeFromLeft(4);
        waveform_.setBounds(area);
    }

private:
    FilePlayerProcessor* player() const { return dynamic_cast<FilePlayerProcessor*>(processor()); }

    void refresh() override
    {
        auto* p = player();
        const bool playing = p != nullptr && p->isPlaying();
        if (playing != shownPlaying_)
        {
            shownPlaying_ = playing;
            playButton_.setShape(playing ? pauseIcon() : playIcon(), false, true, false);
            playButton_.setColours(
                playing ? tokens::accentPrimary : tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary);
        }
        if (p == nullptr)
            return;

        if (const int generation = p->getOverviewGeneration(); generation != overviewGeneration_)
        {
            overviewGeneration_ = generation;
            waveform_.setPeaks(p->getOverview());
        }
        const double length = p->getLengthSeconds();
        juce::String text;
        if (p->isLoading())
            text = "Loading...";
        else if (p->getLoadError().isNotEmpty())
            text = "Can't read the file";
        else if (length <= 0.0)
            text = "No file: ED to choose one";
        else
            text = minutesAndSeconds(p->getPositionSeconds()) + " / " + minutesAndSeconds(length);
        waveform_.setState(length > 0.0 ? p->getPositionSeconds() / length : 0.0, text);
    }

    juce::ShapeButton playButton_{"Play", tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary};
    juce::ShapeButton returnButton_{"Return to Zero", tokens::fgPrimary, tokens::accentPrimary, tokens::accentPrimary};
    Waveform waveform_;
    bool shownPlaying_{false};
    int overviewGeneration_{-1};
};

// ---- Audio Recorder (M6 U7): Record, the take's time, and the input level ------

class RecorderControls final : public ProcessorControls
{
public:
    RecorderControls(AppContext& ctx, session::NodeId node) : ProcessorControls(ctx, node, 30)
    {
        recordButton_.setShape(recordIcon(), false, true, false);
        recordButton_.setTooltip("Record a take: each is a new file, named after the patch and the time. ED sets the "
                                 "folder and Sync.");
        recordButton_.onClick = [this]
        {
            if (auto* r = recorder())
                r->toggleRecord();
            refresh();
        };
        time_.setFont(
            juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        time_.setColour(juce::Label::textColourId, tokens::fgMuted);
        addAndMakeVisible(recordButton_);
        addAndMakeVisible(time_);
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        recordButton_.setBounds(area.removeFromLeft(22).withSizeKeepingCentre(18, 18));
        area.removeFromLeft(4);
        time_.setBounds(area.removeFromLeft(46));
        area.removeFromLeft(4);
        meter_ = area.reduced(0, 9);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bar = meter_.toFloat();
        g.setColour(tokens::bgMuted);
        g.fillRoundedRectangle(bar, 2.0f);
        g.setColour(level_ > 0.9f ? tokens::accentDestructive : tokens::accentPrimary);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, level_)), 2.0f);
    }

private:
    engine::processors::AudioRecorderProcessor* recorder() const
    {
        return dynamic_cast<engine::processors::AudioRecorderProcessor*>(processor());
    }

    void refresh() override
    {
        auto* r = recorder();
        if (r == nullptr)
            return;
        // Takes are named after the playing patch.
        r->setTakePrefix(ctx_.bankDocument.activePatch().name);

        const bool recording = r->isRecording();
        if (recording != shownRecording_)
        {
            shownRecording_ = recording;
            const auto colour = recording ? tokens::accentDestructive : tokens::fgPrimary;
            recordButton_.setColours(colour, tokens::accentDestructive, tokens::accentDestructive);
            time_.setColour(juce::Label::textColourId, recording ? tokens::accentDestructive : tokens::fgMuted);
        }
        const auto text = recording ? minutesAndSeconds(r->getTakeSeconds()) : juce::String("Ready");
        if (time_.getText() != text)
            time_.setText(text, juce::dontSendNotification);

        const float peak = r->takeInputPeak();
        const float next = peak > level_ ? peak : level_ * 0.85f; // fast up, slow down
        if (std::abs(next - level_) > 0.002f)
        {
            level_ = next;
            repaint(meter_);
        }
    }

    juce::ShapeButton recordButton_{"Record", tokens::fgPrimary, tokens::accentDestructive, tokens::accentDestructive};
    juce::Label time_;
    juce::Rectangle<int> meter_;
    float level_{0.0f};
    bool shownRecording_{false};
};

} // namespace

std::unique_ptr<ProcessorControls> makeProcessorControls(AppContext& ctx,
                                                         const juce::String& internalType,
                                                         session::NodeId node)
{
    if (internalType == "OutputToggleProcessor")
        return std::make_unique<OutputToggleControls>(ctx, node);
    if (internalType == "FilePlayerProcessor")
        return std::make_unique<FilePlayerControls>(ctx, node);
    if (internalType == "AudioRecorderProcessor")
        return std::make_unique<RecorderControls>(ctx, node);
    return nullptr;
}

bool hasProcessorEditor(const juce::String& internalType)
{
    return internalType == "FilePlayerProcessor" || internalType == "AudioRecorderProcessor";
}

juce::AudioProcessorEditor* makeProcessorEditor(AppContext& ctx,
                                                juce::AudioProcessor& processor,
                                                const juce::String& internalType)
{
    if (internalType == "FilePlayerProcessor")
        if (auto* player = dynamic_cast<FilePlayerProcessor*>(&processor))
            return new FilePlayerEditor(ctx, *player);
    if (internalType == "AudioRecorderProcessor")
        if (auto* recorder = dynamic_cast<engine::processors::AudioRecorderProcessor*>(&processor))
            return new RecorderEditor(ctx, *recorder);
    return nullptr;
}

} // namespace pedalboard3::ui
