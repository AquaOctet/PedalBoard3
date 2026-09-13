#include "Patch.h"

#include "engine/TransportMath.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <algorithm>
#include <unordered_set>

namespace pedalboard3::session
{

NodeId Patch::nextNodeId() const noexcept
{
    NodeId maxId = 0;
    for (const auto& n : nodes)
        if (n.id > maxId)
            maxId = n.id;
    return maxId + 1;
}

const PatchNode* Patch::findNode(NodeId id) const noexcept
{
    for (const auto& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

PatchNode* Patch::findNode(NodeId id) noexcept
{
    for (auto& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

int Patch::indexOfNode(NodeId id) const noexcept
{
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
        if (nodes[i].id == id)
            return i;
    return -1;
}

namespace
{

// Iterative DFS cycle detector over the directed connection graph (M5 U1: no
// patch can overflow the stack). `graphIO` (== 0) is an external endpoint and
// never visited.
bool hasCycle(const Patch& p)
{
    std::unordered_map<NodeId, std::vector<NodeId>> next;
    for (const auto& c : p.connections)
        if (c.src != graphIO && c.dst != graphIO)
            next[c.src].push_back(c.dst);

    enum class Mark : std::uint8_t
    {
        None,
        Visiting,
        Done,
    };
    std::unordered_map<NodeId, Mark> mark;
    std::vector<std::pair<NodeId, std::size_t>> stack; // a node, and the next of its successors to visit
    for (const auto& n : p.nodes)
    {
        if (mark[n.id] != Mark::None)
            continue;
        mark[n.id] = Mark::Visiting;
        stack.emplace_back(n.id, 0);
        while (!stack.empty())
        {
            const NodeId node = stack.back().first;
            const auto it = next.find(node);
            if (it == next.end() || stack.back().second >= it->second.size())
            {
                mark[node] = Mark::Done;
                stack.pop_back();
                continue;
            }
            const NodeId successor = it->second[stack.back().second++];
            const Mark seen = mark[successor];
            if (seen == Mark::Visiting)
                return true;
            if (seen == Mark::None)
            {
                mark[successor] = Mark::Visiting;
                stack.emplace_back(successor, 0);
            }
        }
    }
    return false;
}

// ---- Mapping (<MAPPINGS>) serialization, shared by patchToXml/patchFromXml ----

// The embedded control::MidiTarget is written as a shared <TARGET> child so the
// MIDI and OSC map elements reuse one encoding.
void writeTarget(juce::XmlElement& mapEl, const control::MidiTarget& t)
{
    auto* tgt = mapEl.createNewChildElement("TARGET");
    tgt->setAttribute("kind", control::midiTargetKindToString(t.kind));
    tgt->setAttribute("node", t.node);
    tgt->setAttribute("parameterIndex", t.parameterIndex);
    tgt->setAttribute("patchIndex", t.patchIndex);
    tgt->setAttribute("commandId", t.commandId);
}

// Fills `t` (the defaults, PluginParameter and node 0, when <TARGET> is absent).
// False for a target kind this version doesn't know (M5 U4, plan KD7): the
// caller keeps that mapping verbatim rather than coercing it to a parameter.
bool readTarget(const juce::XmlElement& mapEl, control::MidiTarget& t)
{
    if (auto* tgt = mapEl.getChildByName("TARGET"))
    {
        if (tgt->hasAttribute("kind"))
        {
            const auto kind = control::midiTargetKindFromString(tgt->getStringAttribute("kind"));
            if (!kind)
                return false;
            t.kind = *kind;
        }
        t.node = tgt->getIntAttribute("node", 0);
        t.parameterIndex = tgt->getIntAttribute("parameterIndex", 0);
        t.patchIndex = tgt->getIntAttribute("patchIndex", 0);
        t.commandId = tgt->getStringAttribute("commandId", "");
    }
    return true;
}

void appendMappings(juce::XmlElement& root,
                    const std::vector<control::MidiMapping>& midi,
                    const std::vector<control::OscMapping>& osc,
                    const UnknownXml* kept)
{
    if (midi.empty() && osc.empty() && (kept == nullptr || kept->isEmpty()))
        return; // omit <MAPPINGS> entirely when there is nothing to persist

    auto* mapsEl = root.createNewChildElement("MAPPINGS");

    for (const auto& m : midi)
    {
        auto* el = mapsEl->createNewChildElement("MIDI_MAP");
        el->setAttribute("kind", control::midiSourceKindToString(m.source.kind));
        el->setAttribute("channel", m.source.channel);
        el->setAttribute("controller", m.source.controller);
        el->setAttribute("rangeMin", static_cast<double>(m.rangeMin));
        el->setAttribute("rangeMax", static_cast<double>(m.rangeMax));
        el->setAttribute("inverted", m.inverted);
        el->setAttribute("latch", m.latch);
        writeTarget(*el, m.target);
    }

    for (const auto& o : osc)
    {
        auto* el = mapsEl->createNewChildElement("OSC_MAP"); // OscMapping has no latch
        el->setAttribute("address", o.address);
        el->setAttribute("argumentIndex", o.argumentIndex);
        el->setAttribute("rangeMin", static_cast<double>(o.rangeMin));
        el->setAttribute("rangeMax", static_cast<double>(o.rangeMax));
        el->setAttribute("inverted", o.inverted);
        writeTarget(*el, o.target);
    }

    if (kept != nullptr)
        kept->writeTo(*mapsEl); // what a newer version wrote here (M5 U4)
}

void readMappings(const juce::XmlElement& root,
                  std::vector<control::MidiMapping>& outMidi,
                  std::vector<control::OscMapping>& outOsc,
                  UnknownXml* unknownOut)
{
    auto* mapsEl = root.getChildByName("MAPPINGS");
    if (mapsEl == nullptr)
        return; // no <MAPPINGS> (every pre-Phase-4 file) -> empty tables, no error

    // A newer version's mappings of a kind this one doesn't know, and anything
    // else it wrote here, are kept whole (M5 U4, plan KD7).
    if (unknownOut != nullptr)
        *unknownOut = UnknownXml::collect(*mapsEl, {}, {"MIDI_MAP", "OSC_MAP"});
    const auto keepWhole = [unknownOut](const juce::XmlElement& el)
    {
        if (unknownOut != nullptr)
            unknownOut->keep(el);
    };

    for (auto* el : mapsEl->getChildIterator())
    {
        if (el->hasTagName("MIDI_MAP"))
        {
            control::MidiMapping m;
            const auto kind = control::midiSourceKindFromString(el->getStringAttribute("kind"));
            if (!kind || !readTarget(*el, m.target))
            {
                keepWhole(*el);
                continue;
            }

            m.source.kind = *kind;
            m.source.channel = el->getIntAttribute("channel", 0);
            m.source.controller = el->getIntAttribute("controller", 0);
            m.rangeMin = static_cast<float>(el->getDoubleAttribute("rangeMin", 0.0));
            m.rangeMax = static_cast<float>(el->getDoubleAttribute("rangeMax", 1.0));
            m.inverted = el->getBoolAttribute("inverted", false);
            m.latch = el->getBoolAttribute("latch", false);
            outMidi.push_back(std::move(m));
        }
        else if (el->hasTagName("OSC_MAP"))
        {
            control::OscMapping o;
            if (!readTarget(*el, o.target))
            {
                keepWhole(*el);
                continue;
            }
            o.address = el->getStringAttribute("address", "");
            o.argumentIndex = el->getIntAttribute("argumentIndex", 0);
            o.rangeMin = static_cast<float>(el->getDoubleAttribute("rangeMin", 0.0));
            o.rangeMax = static_cast<float>(el->getDoubleAttribute("rangeMax", 1.0));
            o.inverted = el->getBoolAttribute("inverted", false);
            outOsc.push_back(std::move(o));
        }
        // any other child tag was collected above (or skipped without unknownOut)
    }
}

} // namespace

void appendMappingsXml(juce::XmlElement& parent,
                       const std::vector<control::MidiMapping>& midi,
                       const std::vector<control::OscMapping>& osc,
                       const UnknownXml* kept)
{
    appendMappings(parent, midi, osc, kept);
}

void readMappingsXml(const juce::XmlElement& parent,
                     std::vector<control::MidiMapping>& midi,
                     std::vector<control::OscMapping>& osc,
                     UnknownXml* unknownOut)
{
    readMappings(parent, midi, osc, unknownOut);
}

PatchValidation validatePatch(const Patch& p) noexcept
{
    // Duplicate NodeIds.
    std::unordered_set<NodeId> seen;
    for (const auto& n : p.nodes)
    {
        if (n.id == graphIO)
            return PatchValidation::DuplicateNodeId; // graphIO is reserved
        if (!seen.insert(n.id).second)
            return PatchValidation::DuplicateNodeId;
    }

    // Connection endpoints must be either graphIO or an existing node.
    for (const auto& c : p.connections)
    {
        if (c.srcChannel < 0 || c.dstChannel < 0)
            return PatchValidation::InvalidChannel;
        if (c.src != graphIO && seen.count(c.src) == 0)
            return PatchValidation::DanglingConnection;
        if (c.dst != graphIO && seen.count(c.dst) == 0)
            return PatchValidation::DanglingConnection;
    }

    // Cycle check on the node-only subgraph (graphIO is an endpoint, not
    // part of any cycle).
    if (hasCycle(p))
        return PatchValidation::Cycle;

    return PatchValidation::Ok;
}

juce::String patchIdentityString(const Patch& p)
{
    juce::String result;
    for (const auto& n : p.nodes)
    {
        // Internal nodes have an empty desc; key them on their type token so
        // different built-ins don't collide (both would hash to "::") in the
        // crash-loop marker.
        if (n.kind == NodeKind::Internal)
            result << "internal::" << n.internalType << "\n";
        else // by ID and name (M5 U3): a board has no plugin paths
            result << n.desc.pluginFormatName << "::" << juce::String::toHexString(n.desc.uniqueId)
                   << "::" << n.desc.name << "\n";
    }
    return result;
}

namespace
{

// The longest <IDENTITY> value kept; anything past it is cut.
constexpr int kMaxIdentityChars = 1024;

// The <IDENTITY> a board carries (M5 U3, plan KD3): what finds the plugin in the
// scanned list, and what its card shows while it's missing. Never a path.
void writeIdentity(juce::XmlElement& el, const juce::PluginDescription& d)
{
    el.setAttribute("format", d.pluginFormatName);
    el.setAttribute("name", d.name);
    el.setAttribute("manufacturer", d.manufacturerName);
    el.setAttribute("version", d.version);
    el.setAttribute("category", d.category);
    el.setAttribute("uniqueId", juce::String::toHexString(d.uniqueId));
    el.setAttribute("uid", juce::String::toHexString(d.deprecatedUid));
    el.setAttribute("isInstrument", d.isInstrument);
    el.setAttribute("numInputs", d.numInputChannels);
    el.setAttribute("numOutputs", d.numOutputChannels);
}

// A value that names a file or a URL rather than a plugin.
bool looksLikePath(const juce::String& value)
{
    if (value.startsWithChar('/') || value.startsWithChar('\\') || value.contains("://"))
        return true;
    return value.length() >= 3 && juce::CharacterFunctions::isLetter(value[0]) && value[1] == ':' &&
           (value[2] == '\\' || value[2] == '/');
}

juce::String identityText(const juce::XmlElement& el, const char* attribute)
{
    const auto value = el.getStringAttribute(attribute).substring(0, kMaxIdentityChars);
    return looksLikePath(value) ? juce::String() : value;
}

// Reads only what writeIdentity writes; any other attribute is ignored, and a
// path-like value is dropped. An older board's `file=` becomes the node's
// fileHint, which is never opened.
void readIdentity(const juce::XmlElement& el, PatchNode& node)
{
    auto& d = node.desc;
    d.pluginFormatName = identityText(el, "format");
    d.name = identityText(el, "name");
    d.descriptiveName = d.name;
    d.manufacturerName = identityText(el, "manufacturer");
    d.version = identityText(el, "version");
    d.category = identityText(el, "category");
    d.uniqueId = el.getStringAttribute("uniqueId", "0").getHexValue32();
    d.deprecatedUid = el.getStringAttribute("uid", "0").getHexValue32();
    d.isInstrument = el.getBoolAttribute("isInstrument", false);
    d.numInputChannels = juce::jlimit(0, 64, el.getIntAttribute("numInputs"));
    d.numOutputChannels = juce::jlimit(0, 64, el.getIntAttribute("numOutputs"));
    node.fileHint = el.getStringAttribute("file").substring(0, kMaxIdentityChars);
}

} // namespace

std::unique_ptr<juce::XmlElement> patchToXml(const Patch& p)
{
    auto root = std::make_unique<juce::XmlElement>("PATCH");
    root->setAttribute("name", p.name);
    root->setAttribute("tempo", p.tempo);
    root->setAttribute("timeSigNumerator", p.timeSigNumerator);
    root->setAttribute("timeSigDenominator", p.timeSigDenominator);

    auto* nodesEl = root->createNewChildElement("NODES");
    for (const auto& n : p.nodes)
    {
        auto* nodeEl = nodesEl->createNewChildElement("NODE");
        nodeEl->setAttribute("id", n.id);
        nodeEl->setAttribute("bypass", n.bypass);
        nodeEl->setAttribute("customName", n.customName);
        if (n.midiChannel != 0)
            nodeEl->setAttribute("midiChannel", n.midiChannel);

        // Canvas position — only written when set, so unpositioned nodes (and
        // pre-position files) stay attribute-free and auto-lay-out on load.
        if (n.hasPosition())
        {
            nodeEl->setAttribute("x", static_cast<double>(n.x));
            nodeEl->setAttribute("y", static_cast<double>(n.y));
        }

        if (n.kind == NodeKind::Internal)
        {
            // Built-in node: identity is the type token; no <IDENTITY> plugin
            // descriptor. Plugin nodes carry no `kind` attribute so existing
            // plugin-only .pdl output stays byte-identical (read defaults Plugin).
            nodeEl->setAttribute("kind", "internal");
            nodeEl->setAttribute("type", n.internalType);
        }
        else
        {
            writeIdentity(*nodeEl->createNewChildElement("IDENTITY"), n.desc);
        }

        if (n.state.getSize() > 0)
        {
            auto* stateEl = nodeEl->createNewChildElement("STATE");
            stateEl->setAttribute("encoding", "base64");
            stateEl->addTextElement(n.state.toBase64Encoding());
        }

        n.unknown.writeTo(*nodeEl); // what a newer version wrote on the node (M5 U4)
    }

    auto* connsEl = root->createNewChildElement("CONNECTIONS");
    for (const auto& c : p.connections)
    {
        auto* connEl = connsEl->createNewChildElement("CONN");
        connEl->setAttribute("src", c.src);
        connEl->setAttribute("srcChan", juce::String(c.srcChannel));
        connEl->setAttribute("dst", c.dst);
        connEl->setAttribute("dstChan", juce::String(c.dstChannel));
    }

    appendMappings(*root, p.midiMappings, p.oscMappings, &p.unknownMappings);
    p.unknown.writeTo(*root); // what a newer version wrote on the patch (M5 U4)

    return root;
}

std::vector<PatchConnection> linearChainConnections(const std::vector<PatchNode>& nodes)
{
    constexpr int kStereoChannels = 2;
    constexpr int kMidiChannel = juce::AudioProcessorGraph::midiChannelIndex;

    std::vector<PatchConnection> conns;

    if (nodes.empty())
    {
        // Audio + MIDI passthrough so a cleared chain still routes in -> out.
        conns.push_back({graphIO, 0, graphIO, 0});
        conns.push_back({graphIO, 1, graphIO, 1});
        conns.push_back({graphIO, kMidiChannel, graphIO, kMidiChannel});
        return conns;
    }

    // Stereo audio: in -> first node, last -> out, intermediates form the chain.
    for (int ch = 0; ch < kStereoChannels; ++ch)
    {
        conns.push_back({graphIO, ch, nodes.front().id, ch});
        for (size_t i = 0; i + 1 < nodes.size(); ++i)
            conns.push_back({nodes[i].id, ch, nodes[i + 1].id, ch});
        conns.push_back({nodes.back().id, ch, graphIO, ch});
    }

    // MIDI follows the same shape (master plan §4.5 v2 parity, not broadcast).
    conns.push_back({graphIO, kMidiChannel, nodes.front().id, kMidiChannel});
    for (size_t i = 0; i + 1 < nodes.size(); ++i)
        conns.push_back({nodes[i].id, kMidiChannel, nodes[i + 1].id, kMidiChannel});
    conns.push_back({nodes.back().id, kMidiChannel, graphIO, kMidiChannel});

    return conns;
}

std::unique_ptr<Patch> patchFromXml(const juce::XmlElement& root, juce::String* error)
{
    const auto fail = [error](const juce::String& why) -> std::unique_ptr<Patch>
    {
        if (error != nullptr)
            *error = why;
        return nullptr;
    };
    const auto overLimit = [&fail](int count, int limit, const juce::String& what)
    { return fail("it has " + juce::String(count) + " " + what + "; the limit is " + juce::String(limit)); };
    const auto countChildren = [](const juce::XmlElement& parent, const char* tag)
    {
        int count = 0;
        for ([[maybe_unused]] auto* child : parent.getChildWithTagNameIterator(tag))
            ++count;
        return count;
    };

    // Accept the current <PATCH> tag and the legacy <ACTIVE_PATCH> tag
    // that pre-Phase-2 builds wrote into Preferences.
    if (!root.hasTagName("PATCH") && !root.hasTagName("ACTIVE_PATCH"))
        return nullptr;

    auto patch = std::make_unique<Patch>();
    patch->name = root.getStringAttribute("name", "Untitled");
    // What a newer version wrote on the patch, kept for the save (M5 U4, plan KD7).
    patch->unknown = UnknownXml::collect(
        root, {"name", "tempo", "timeSigNumerator", "timeSigDenominator"}, {"NODES", "CONNECTIONS", "MAPPINGS"});

    // Tempo and time signature (schemaMinor 6): absent or unusable values fall
    // back to 120 BPM in 4/4.
    const double tempo = root.getDoubleAttribute("tempo", engine::transport::kDefaultTempo);
    patch->tempo = std::isfinite(tempo) ? engine::transport::clampTempo(tempo) : engine::transport::kDefaultTempo;
    const int numerator = root.getIntAttribute("timeSigNumerator", 4);
    const int denominator = root.getIntAttribute("timeSigDenominator", 4);
    if (engine::transport::isValidTimeSignature(numerator, denominator))
    {
        patch->timeSigNumerator = numerator;
        patch->timeSigDenominator = denominator;
    }

    if (auto* nodesEl = root.getChildByName("NODES"))
    {
        if (const int count = countChildren(*nodesEl, "NODE"); count > kMaxNodesPerPatch)
            return overLimit(count, kMaxNodesPerPatch, "plugins");
        for (auto* nodeEl : nodesEl->getChildIterator())
        {
            if (!nodeEl->hasTagName("NODE"))
                continue;

            PatchNode n;
            n.id = nodeEl->getIntAttribute("id");
            n.bypass = nodeEl->getBoolAttribute("bypass");
            n.customName = nodeEl->getStringAttribute("customName");
            n.midiChannel = juce::jlimit(0, 16, nodeEl->getIntAttribute("midiChannel", 0));
            // What a newer version wrote on the node (M5 U4). <IDENTITY> keeps its
            // allowlist (M5 U3): nothing unknown there is kept.
            n.unknown = UnknownXml::collect(*nodeEl,
                                            {"id", "bypass", "customName", "midiChannel", "x", "y", "kind", "type"},
                                            {"IDENTITY", "STATE"});

            // Canvas position — absent attributes leave x/y as NaN (unpositioned),
            // so pre-position files auto-lay-out. Both must be present to count.
            if (nodeEl->hasAttribute("x") && nodeEl->hasAttribute("y"))
            {
                n.x = static_cast<float>(nodeEl->getDoubleAttribute("x"));
                n.y = static_cast<float>(nodeEl->getDoubleAttribute("y"));
            }

            // `kind` absent => Plugin (back-compat with pre-discriminator files).
            if (nodeEl->getStringAttribute("kind", "plugin") == "internal")
            {
                // Built-in node: identity is the type token, no <IDENTITY> — so a
                // legitimately absent <IDENTITY> never fails the load.
                n.kind = NodeKind::Internal;
                n.internalType = nodeEl->getStringAttribute("type");
            }
            else if (auto* identityEl = nodeEl->getChildByName("IDENTITY"))
            {
                readIdentity(*identityEl, n);
            }

            if (auto* stateEl = nodeEl->getChildByName("STATE"))
            {
                // Checked before decoding (M5 U1, AC-SE-8): at most kMaxStateBytes,
                // and a damaged blob is an error rather than half a plugin's settings.
                const auto encoded = stateEl->getAllSubText().trim();
                if (static_cast<juce::int64>(encoded.length()) > kMaxStateBytes / 3 * 4 + 64)
                    return fail("a plugin's saved settings are larger than " +
                                juce::String(kMaxStateBytes / (1024 * 1024)) + " MB");
                if (encoded.isNotEmpty() && !n.state.fromBase64Encoding(encoded))
                    return fail("a plugin's saved settings are damaged");
            }

            patch->nodes.push_back(std::move(n));
        }
    }

    if (auto* connsEl = root.getChildByName("CONNECTIONS"))
    {
        if (const int count = countChildren(*connsEl, "CONN"); count > kMaxConnectionsPerPatch)
            return overLimit(count, kMaxConnectionsPerPatch, "connections");
        for (auto* connEl : connsEl->getChildIterator())
        {
            if (!connEl->hasTagName("CONN"))
                continue;

            PatchConnection c;
            c.src = connEl->getIntAttribute("src");
            c.srcChannel = connEl->getIntAttribute("srcChan");
            c.dst = connEl->getIntAttribute("dst");
            c.dstChannel = connEl->getIntAttribute("dstChan");
            patch->connections.push_back(c);
        }
    }

    if (auto* mappingsEl = root.getChildByName("MAPPINGS"))
        if (const int count = mappingsEl->getNumChildElements(); count > kMaxMappingsPerPatch)
            return overLimit(count, kMaxMappingsPerPatch, "mappings");
    readMappings(root, patch->midiMappings, patch->oscMappings, &patch->unknownMappings);

    return patch;
}

} // namespace pedalboard3::session
