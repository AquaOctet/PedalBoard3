// A stand-in for pb3-scanner-helper, for PluginScanner's tests (M5 U6): plain
// C++ with no JUCE, so its cases run on a headless CI runner. What it does
// depends on a word in the plugin path it's handed:
//
//   hang        write its process id beside the result file, then sleep until killed
//   crash       die, as a plugin crashing the helper would
//   flood       print 1 MB to stdout and to stderr, then write a 1 MB result
//   instrument  an instrument-only result
//   effect      one effect
//   otherwise   an empty result: not a plugin
//
// Usage: pb3-fake-scanner-helper <plugin-path> <result-file>

#include "engine/ScanProtocol.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{

long processId()
{
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

std::string pluginXml(const std::string& name, const std::string& path, bool instrument, const std::string& padding)
{
    std::string xml = "<PLUGIN name=\"" + name + "\" format=\"VST3\" category=\"Fx\"" +
                      " manufacturer=\"Pedalboard 3 Tests\" version=\"1.0\" file=\"" + path + "\"" +
                      " uniqueId=\"1234abcd\" isInstrument=\"" + (instrument ? "1" : "0") + "\"" +
                      " numInputs=\"2\" numOutputs=\"2\"";
    if (!padding.empty())
        xml += " descriptiveName=\"" + padding + "\"";
    return xml + "/>";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fputs("usage: pb3-fake-scanner-helper <plugin-path> <result-file>\n", stderr);
        return 64;
    }
    const std::string path = argv[1];
    const std::string resultFile = argv[2];
    const auto has = [&path](const char* word) { return path.find(word) != std::string::npos; };

    if (has("hang"))
    {
        std::ofstream(resultFile + ".pid") << processId() << std::flush;
        for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (has("crash"))
    {
#if defined(_WIN32)
        std::_Exit(3); // what abort() leaves, without the debug CRT's dialog
#else
        std::abort(); // killed by a signal: exit code 0 and no sentinel, as a real crash
#endif
    }

    std::string plugins;
    if (has("flood"))
    {
        const std::string megabyte(1024 * 1024, 'x');
        std::fwrite(megabyte.data(), 1, megabyte.size(), stdout);
        std::fwrite(megabyte.data(), 1, megabyte.size(), stderr);
        std::fflush(stdout);
        plugins = pluginXml("Flood", path, false, megabyte);
    }
    else if (has("instrument"))
    {
        plugins = pluginXml("FakeSynth", path, true, {});
    }
    else if (has("effect"))
    {
        plugins = pluginXml("FakeEQ", path, false, {});
    }

    std::ofstream out(resultFile, std::ios::binary);
    out << "<PB3_SCAN_RESULT>" << plugins << "</PB3_SCAN_RESULT>\n" << pedalboard3::engine::kScanOkSentinel << "\n";
    return 0;
}
