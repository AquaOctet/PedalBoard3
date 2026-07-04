#pragma once

// Shared contract between the out-of-process scanner helper
// (tools/pb3-scanner-helper.cpp) and its parent (engine::PluginScanner).
// Header-only and dependency-free (no JUCE) so the console helper — which
// deliberately links none of the engine — can include it without pulling in
// the audio graph. Keeping the wire tokens here means the helper and parser
// share ONE definition instead of two literals with a "keep in sync" comment.

namespace pedalboard3::engine
{

// The helper prints this as its very last stdout line, after JUCE teardown
// survives. On POSIX a signal-killed process still reports exit code 0, so a
// plugin that flushes valid descriptors then crashes in teardown would look
// like a clean scan; PluginScanner::parseHelperOutput refuses any output
// missing this sentinel and blacklists the path instead (crash-hunt #6).
inline constexpr const char* kScanOkSentinel = "<PB3_SCAN_OK/>";

} // namespace pedalboard3::engine
