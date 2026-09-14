#pragma once

#include <juce_core/juce_core.h>

namespace pb3test
{

// A settings file of a test's own, in a fresh temp folder that goes when this
// does. Every test that makes a session::Preferences passes it `file()`: a test
// must never open the user's real settings (Preferences::defaultSettingsFile()),
// which the Preferences tests once did, wiping the developer's plugin list and
// session on every run. support/RealSettingsGuard.cpp fails a run that touches
// them.
//
// Declare it before the Preferences it serves, so the Preferences (which saves
// as it goes) is destroyed first.
class TempSettings
{
public:
    explicit TempSettings(const juce::String& tag = "test")
        : folder_(juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getNonexistentChildFile("pb3-settings-" + tag, {}, false))
    {
        folder_.createDirectory();
    }

    ~TempSettings() { folder_.deleteRecursively(); }

    juce::File file() const { return folder_.getChildFile("Pedalboard 3.settings"); }
    const juce::File& folder() const noexcept { return folder_; }

private:
    juce::File folder_;

    JUCE_DECLARE_NON_COPYABLE(TempSettings)
};

} // namespace pb3test
