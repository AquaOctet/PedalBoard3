// Tests for engine::scanFileInMessageLoop (engine/ScanInMessageLoop.h): the
// scanner helper scans with a message loop running, so a format that needs
// the message thread free (AUv3) is scanned off it (#3), and every other
// format still scans on it.

#include <catch2/catch_test_macros.hpp>

#include "engine/ScanInMessageLoop.h"

#include <atomic>
#include <memory>

namespace
{

// A plugin format that records where it was scanned. With
// `needsFreeMessageThread` it behaves like JUCE's AU format for an AUv3: on the
// message thread it finds nothing, and off it, finishing the "instance" needs
// the message thread to run a callback.
class FakeFormat final : public juce::AudioPluginFormat
{
public:
    FakeFormat(juce::String name, bool needsFreeMessageThread, bool readsFile = true)
        : name_(std::move(name)), needsFree_(needsFreeMessageThread), readsFile_(readsFile)
    {
    }

    std::atomic<int> scans{0};
    std::atomic<bool> scannedOnMessageThread{false};

    juce::String getName() const override { return name_; }

    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results, const juce::String& file) override
    {
        ++scans;
        const bool onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
        scannedOnMessageThread = onMessageThread;
        if (needsFree_)
        {
            if (onMessageThread)
                return; // what AudioUnitPluginFormat does for an AUv3
            juce::WaitableEvent created;
            juce::MessageManager::callAsync([&created] { created.signal(); });
            if (!created.wait(5000))
                return; // the message loop wasn't running
        }
        auto description = std::make_unique<juce::PluginDescription>();
        description->name = name_ + " plugin";
        description->pluginFormatName = name_;
        description->fileOrIdentifier = file;
        results.add(description.release());
    }

    bool fileMightContainThisPluginType(const juce::String&) override { return readsFile_; }
    juce::String getNameOfPluginFromIdentifier(const juce::String& identifier) override { return identifier; }
    bool pluginNeedsRescanning(const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist(const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription&) const override
    {
        return needsFree_;
    }

protected:
    void createPluginInstance(const juce::PluginDescription&, double, int, PluginCreationCallback callback) override
    {
        callback(nullptr, "not a real format");
    }

private:
    juce::String name_;
    bool needsFree_;
    bool readsFile_;
};

// Scan on this thread as the message thread, as the helper does, then drop the
// MessageManager: the scan stops its dispatch loop for good, and a later test
// in the same process needs a fresh one.
juce::OwnedArray<juce::PluginDescription> scan(const juce::Array<juce::AudioPluginFormat*>& formats)
{
    juce::MessageManager::getInstance();
    juce::OwnedArray<juce::PluginDescription> results;
    pedalboard3::engine::scanFileInMessageLoop(formats, "/Library/Audio/Plug-Ins/Components/Fake.component", results);
    juce::MessageManager::deleteInstance();
    return results;
}

} // namespace

TEST_CASE("ScanInMessageLoop: an AUv3-style format is scanned off the message thread, with the loop free",
          "[engine][plugin-scanner]")
{
    FakeFormat auv3("AUv3", /*needsFreeMessageThread*/ true);
    const auto results = scan({&auv3});
    REQUIRE(results.size() == 1);
    REQUIRE(results[0]->name == "AUv3 plugin");
    REQUIRE_FALSE(auv3.scannedOnMessageThread.load());
}

TEST_CASE("ScanInMessageLoop: every other format still scans on the message thread", "[engine][plugin-scanner]")
{
    FakeFormat vst3("VST3", /*needsFreeMessageThread*/ false);
    const auto results = scan({&vst3});
    REQUIRE(results.size() == 1);
    REQUIRE(results[0]->name == "VST3 plugin");
    REQUIRE(vst3.scannedOnMessageThread.load());
}

TEST_CASE("ScanInMessageLoop: formats scan in order, and one that can't read the file is skipped",
          "[engine][plugin-scanner]")
{
    FakeFormat vst3("VST3", false);
    FakeFormat other("Other", false, /*readsFile*/ false);
    FakeFormat auv3("AUv3", true);
    const auto results = scan({&vst3, &other, &auv3});
    REQUIRE(results.size() == 2);
    REQUIRE(results[0]->name == "VST3 plugin");
    REQUIRE(results[1]->name == "AUv3 plugin");
    REQUIRE(other.scans.load() == 0);
}

TEST_CASE("ScanInMessageLoop: with no format that reads the file, the loop still ends", "[engine][plugin-scanner]")
{
    FakeFormat other("Other", false, /*readsFile*/ false);
    REQUIRE(scan({&other}).isEmpty());
    REQUIRE(scan({}).isEmpty());
}
