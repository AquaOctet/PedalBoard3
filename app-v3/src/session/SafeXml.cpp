#include "SafeXml.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace pedalboard3::session::safexml
{

namespace
{
Parsed failed(const juce::String& why)
{
    Parsed parsed;
    parsed.error = why;
    return parsed;
}
} // namespace

int nestingDepth(const juce::String& text)
{
    const std::string s = text.toStdString();
    const auto skipPast = [&s](std::size_t from, const char* end)
    {
        const auto at = s.find(end, from);
        return at == std::string::npos ? s.size() : at + std::strlen(end);
    };

    int depth = 0;
    int deepest = 0;
    std::size_t i = 0;
    while (i < s.size())
    {
        if (s[i] != '<')
        {
            ++i;
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0)
        {
            i = skipPast(i + 4, "-->");
            continue;
        }
        if (s.compare(i, 9, "<![CDATA[") == 0)
        {
            i = skipPast(i + 9, "]]>");
            continue;
        }
        if (s.compare(i, 2, "<?") == 0)
        {
            i = skipPast(i + 2, "?>");
            continue;
        }
        if (s.compare(i, 2, "<!") == 0)
        {
            i = skipPast(i + 2, ">");
            continue;
        }

        // A tag: find its end, stepping over quoted attribute values.
        const bool closing = s.compare(i, 2, "</") == 0;
        std::size_t j = i + 1;
        char quote = 0;
        for (; j < s.size(); ++j)
        {
            const char c = s[j];
            if (quote != 0)
            {
                if (c == quote)
                    quote = 0;
            }
            else if (c == '"' || c == '\'')
            {
                quote = c;
            }
            else if (c == '>')
            {
                break;
            }
        }
        const bool selfClosing = !closing && j < s.size() && s[j - 1] == '/';
        if (closing)
            depth = std::max(0, depth - 1);
        else if (selfClosing)
            deepest = std::max(deepest, depth + 1);
        else
            deepest = std::max(deepest, ++depth);
        i = j + 1;
    }
    return deepest;
}

Parsed parseText(const juce::String& text)
{
    if (text.containsIgnoreCase("<!DOCTYPE") || text.containsIgnoreCase("<!ENTITY"))
        return failed("The file declares a DTD or entities, which Pedalboard 3 files never contain, so it was not "
                      "opened.");
    if (nestingDepth(text) > kMaxDepth)
        return failed("The file nests more than " + juce::String(kMaxDepth) +
                      " levels deep, which Pedalboard 3 files never do, so it was not opened.");

    Parsed parsed;
    parsed.xml = juce::parseXML(text); // the String overload: no input source, nothing outside the text
    if (parsed.xml == nullptr)
        parsed.error = "The file isn't valid XML. It may be truncated or damaged.";
    return parsed;
}

Parsed parseFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return failed("File not found: " + file.getFullPathName());

    const auto tooLarge = [](juce::int64 bytes)
    {
        return failed("The file is " + juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0), 1) +
                      " MB. Pedalboard 3 opens files up to " + juce::String(kMaxFileBytes / (1024 * 1024)) + " MB.");
    };
    if (const auto size = file.getSize(); size > kMaxFileBytes)
        return tooLarge(size);

    juce::MemoryBlock data;
    if (!file.loadFileAsData(data))
        return failed("Couldn't read " + file.getFullPathName());
    if (static_cast<juce::int64>(data.getSize()) > kMaxFileBytes) // it grew while being read
        return tooLarge(static_cast<juce::int64>(data.getSize()));

    return parseText(juce::String::createStringFromData(data.getData(), static_cast<int>(data.getSize())));
}

} // namespace pedalboard3::session::safexml
