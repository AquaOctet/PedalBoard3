#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace pedalboard3::engine
{

// The one background thread the built-in processors share for disk work (M6
// U6, plan KD8): decoding a File Player's file now, and writing takes and
// growing loop memory later. The audio thread never touches the disk. It also
// holds the audio formats they read and write.
//
// PluginHost owns it. It starts with PluginHost, because processors are
// created on Session's worker thread, and it must stop only once every
// processor that registered with it has gone.
//
// That is a teardown order, and it is not the obvious one. The processors do
// not belong to Session — Session's destructor says so, and leaves them be —
// they belong to the graph, which AudioEngine owns. So **AudioEngine has to be
// destroyed before PluginHost**, which is not reverse construction order.
// Get it wrong and ~FilePlayerProcessor calls removeTimeSliceClient on a
// destroyed thread: an access violation on the way out, only when a board had
// one of these four nodes on it. App::shutdown does it in the right order and
// says why.
class DiskThread
{
public:
    DiskThread() : thread_("Pedalboard 3 disk")
    {
        formats_.registerBasicFormats();
        thread_.startThread();
    }

    ~DiskThread() { thread_.stopThread(2000); }

    juce::TimeSliceThread& thread() noexcept { return thread_; }
    juce::AudioFormatManager& formats() noexcept { return formats_; }

private:
    juce::TimeSliceThread thread_;
    juce::AudioFormatManager formats_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiskThread)
};

} // namespace pedalboard3::engine
