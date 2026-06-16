#include "Patch.h"

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

// Recursive DFS cycle detector over the directed connection graph.
// `graphIO` (== 0) is treated as an external endpoint and never visited.
bool hasCycleFrom(NodeId start,
                  const std::vector<PatchConnection>& connections,
                  std::unordered_set<NodeId>& visiting,
                  std::unordered_set<NodeId>& done) noexcept
{
    if (start == graphIO)
        return false;
    if (done.count(start) != 0)
        return false;
    if (visiting.count(start) != 0)
        return true;

    visiting.insert(start);
    for (const auto& c : connections)
    {
        if (c.src == start && c.dst != graphIO)
        {
            if (hasCycleFrom(c.dst, connections, visiting, done))
                return true;
        }
    }
    visiting.erase(start);
    done.insert(start);
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

control::MidiTarget readTarget(const juce::XmlElement& mapEl)
{
    control::MidiTarget t; // defaults (PluginParameter, node 0) if <TARGET> absent
    if (auto* tgt = mapEl.getChildByName("TARGET"))
    {
        if (auto kind = control::midiTargetKindFromString(tgt->getStringAttribute("kind")))
            t.kind = *kind; // unknown target kind keeps the PluginParameter default
        t.node = tgt->getIntAttribute("node", 0);
        t.parameterIndex = tgt->getIntAttribute("parameterIndex", 0);
        t.patchIndex = tgt->getIntAttribute("patchIndex", 0);
        t.commandId = tgt->getStringAttribute("commandId", "");
    }
    return t;
}

void appendMappings(juce::XmlElement& root,
                    const std::vector<control::MidiMapping>& midi,
                    const std::vector<control::OscMapping>& osc)
{
    if (midi.empty() && osc.empty())
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
}

void readMappings(const juce::XmlElement& root,
                  std::vector<control::MidiMapping>& outMidi,
                  std::vector<control::OscMapping>& outOsc)
{
    auto* mapsEl = root.getChildByName("MAPPINGS");
    if (mapsEl == nullptr)
        return; // no <MAPPINGS> (every pre-Phase-4 file) -> empty tables, no error

    for (auto* el : mapsEl->getChildIterator())
    {
        if (el->hasTagName("MIDI_MAP"))
        {
            const auto kind = control::midiSourceKindFromString(el->getStringAttribute("kind"));
            if (!kind)
                continue; // drop a single mapping with an unknown source kind

            control::MidiMapping m;
            m.source.kind = *kind;
            m.source.channel = el->getIntAttribute("channel", 0);
            m.source.controller = el->getIntAttribute("controller", 0);
            m.rangeMin = static_cast<float>(el->getDoubleAttribute("rangeMin", 0.0));
            m.rangeMax = static_cast<float>(el->getDoubleAttribute("rangeMax", 1.0));
            m.inverted = el->getBoolAttribute("inverted", false);
            m.latch = el->getBoolAttribute("latch", false);
            m.target = readTarget(*el);
            outMidi.push_back(std::move(m));
        }
        else if (el->hasTagName("OSC_MAP"))
        {
            control::OscMapping o;
            o.address = el->getStringAttribute("address", "");
            o.argumentIndex = el->getIntAttribute("argumentIndex", 0);
            o.rangeMin = static_cast<float>(el->getDoubleAttribute("rangeMin", 0.0));
            o.rangeMax = static_cast<float>(el->getDoubleAttribute("rangeMax", 1.0));
            o.inverted = el->getBoolAttribute("inverted", false);
            o.target = readTarget(*el);
            outOsc.push_back(std::move(o));
        }
        // any other child tag is skipped
    }
}

} // namespace

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
        if (c.src != graphIO && p.findNode(c.src) == nullptr)
            return PatchValidation::DanglingConnection;
        if (c.dst != graphIO && p.findNode(c.dst) == nullptr)
            return PatchValidation::DanglingConnection;
    }

    // Cycle check on the node-only subgraph (graphIO is an endpoint, not
    // part of any cycle).
    std::unordered_set<NodeId> visiting;
    std::unordered_set<NodeId> done;
    for (const auto& n : p.nodes)
        if (hasCycleFrom(n.id, p.connections, visiting, done))
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
        else
            result << n.desc.pluginFormatName << "::" << n.desc.fileOrIdentifier << "\n";
    }
    return result;
}

std::unique_ptr<juce::XmlElement> patchToXml(const Patch& p)
{
    auto root = std::make_unique<juce::XmlElement>("PATCH");
    root->setAttribute("name", p.name);

    auto* nodesEl = root->createNewChildElement("NODES");
    for (const auto& n : p.nodes)
    {
        auto* nodeEl = nodesEl->createNewChildElement("NODE");
        nodeEl->setAttribute("id", n.id);
        nodeEl->setAttribute("bypass", n.bypass);
        nodeEl->setAttribute("customName", n.customName);

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
        else if (auto descXml = n.desc.createXml())
        {
            // Re-tag from <PLUGIN> to <IDENTITY> so the schema reads
            // cleanly (master plan §4.5 idiom).
            descXml->setTagName("IDENTITY");
            nodeEl->addChildElement(descXml.release());
        }

        if (n.state.getSize() > 0)
        {
            auto* stateEl = nodeEl->createNewChildElement("STATE");
            stateEl->setAttribute("encoding", "base64");
            stateEl->addTextElement(n.state.toBase64Encoding());
        }
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

    appendMappings(*root, p.midiMappings, p.oscMappings);

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

std::unique_ptr<Patch> patchFromXml(const juce::XmlElement& root)
{
    // Accept the current <PATCH> tag and the legacy <ACTIVE_PATCH> tag
    // that pre-Phase-2 builds wrote into Preferences.
    if (!root.hasTagName("PATCH") && !root.hasTagName("ACTIVE_PATCH"))
        return nullptr;

    auto patch = std::make_unique<Patch>();
    patch->name = root.getStringAttribute("name", "Untitled");

    if (auto* nodesEl = root.getChildByName("NODES"))
    {
        for (auto* nodeEl : nodesEl->getChildIterator())
        {
            if (!nodeEl->hasTagName("NODE"))
                continue;

            PatchNode n;
            n.id = nodeEl->getIntAttribute("id");
            n.bypass = nodeEl->getBoolAttribute("bypass");
            n.customName = nodeEl->getStringAttribute("customName");

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
                // loadFromXml expects a <PLUGIN> root.
                juce::XmlElement asPlugin(*identityEl);
                asPlugin.setTagName("PLUGIN");
                if (!n.desc.loadFromXml(asPlugin))
                    return nullptr;
            }

            if (auto* stateEl = nodeEl->getChildByName("STATE"))
                n.state.fromBase64Encoding(stateEl->getAllSubText());

            patch->nodes.push_back(std::move(n));
        }
    }

    if (auto* connsEl = root.getChildByName("CONNECTIONS"))
    {
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

    readMappings(root, patch->midiMappings, patch->oscMappings);

    return patch;
}

} // namespace pedalboard3::session
