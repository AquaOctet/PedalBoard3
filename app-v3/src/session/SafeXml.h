#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace pedalboard3::session::safexml
{

// The rules every board file is parsed under (M5 U1, master plan AC-SE-7 /
// AC-SE-8):
//   - At most kMaxFileBytes are read from disk. A larger file is refused before
//     it's read.
//   - No DTDs and no entity declarations: a <!DOCTYPE or <!ENTITY anywhere is
//     refused. The text is parsed with juce::parseXML(String), which has no
//     input source, so nothing outside it can be loaded. (juce::parseXML(File)
//     loads SYSTEM DTDs and entities relative to the file.)
//   - At most kMaxDepth levels of nesting, so no file can overflow the parser's
//     stack.
inline constexpr juce::int64 kMaxFileBytes = 32 * 1024 * 1024;
inline constexpr int kMaxDepth = 64;

struct Parsed
{
    std::unique_ptr<juce::XmlElement> xml; // nullptr on failure
    juce::String error;                    // why, in words for the user
};

Parsed parseText(const juce::String& text);
Parsed parseFile(const juce::File& file);

// How deeply `text`'s elements nest. It steps over comments, CDATA, processing
// instructions and quoted attribute values. Exposed for the tests.
int nestingDepth(const juce::String& text);

} // namespace pedalboard3::session::safexml
