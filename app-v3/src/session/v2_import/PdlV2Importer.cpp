#include "PdlV2Importer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>
#include <unordered_map>

namespace pedalboard3::session::v2
{

namespace
{

// v2 internal I/O filters that become the v3 graphIO endpoint rather than a
// chain node. Matched on the <PLUGIN format="Internal"> name.
bool isInternalIoPlugin(const juce::XmlElement& pluginEl)
{
    if (pluginEl.getStringAttribute("format") != "Internal")
        return false;
    const auto name = pluginEl.getStringAttribute("name");
    return name == "Audio Input" || name == "Audio Output" || name == "Midi Input" || name == "Midi Output";
}

juce::PluginDescription descFromV2Plugin(const juce::XmlElement& pluginEl)
{
    juce::PluginDescription d;
    d.name = pluginEl.getStringAttribute("name");
    d.descriptiveName = pluginEl.getStringAttribute("descriptiveName", d.name);
    d.manufacturerName = pluginEl.getStringAttribute("manufacturer");
    d.pluginFormatName = pluginEl.getStringAttribute("format"); // e.g. "VST" (VST2)
    d.category = pluginEl.getStringAttribute("category");
    d.version = pluginEl.getStringAttribute("version");
    d.fileOrIdentifier = pluginEl.getStringAttribute("file");
    d.isInstrument = pluginEl.getIntAttribute("isInstrument", 0) != 0;
    d.numInputChannels = pluginEl.getIntAttribute("numInputs", 2);
    d.numOutputChannels = pluginEl.getIntAttribute("numOutputs", 2);
    // v2 stores the VST2 4-char code as hex (e.g. "46513370").
    const auto uid = pluginEl.getStringAttribute("uid").getHexValue32();
    d.uniqueId = uid;
    d.deprecatedUid = uid;
    return d;
}

// Reorder `nodes` to follow the audio (channel 0) signal chain starting from the
// input endpoint, using the translated connections. Nodes not reached by the
// walk (parallel/branching graphs, or disconnected) keep their original order
// and are appended after the reachable run. Returns false if the result isn't a
// single linear pass over every node (so the caller can warn).
bool orderNodesBySignalFlow(std::vector<PatchNode>& nodes, const std::vector<PatchConnection>& connections)
{
    if (nodes.size() < 2)
        return true;

    std::unordered_map<NodeId, NodeId> succ; // src -> dst on audio channel 0
    for (const auto& c : connections)
        if (c.srcChannel == 0)
            succ.emplace(c.src, c.dst);

    std::vector<PatchNode> ordered;
    ordered.reserve(nodes.size());
    std::unordered_map<NodeId, bool> placed;

    NodeId cur = graphIO; // start at the input endpoint
    auto step = succ.find(cur);
    int guard = 0;
    const int maxSteps = static_cast<int>(nodes.size()) + 2;
    while (step != succ.end() && step->second != graphIO && guard++ < maxSteps)
    {
        const NodeId id = step->second;
        if (placed[id])
            break; // cycle guard (shouldn't happen for a valid chain)

        for (const auto& nn : nodes)
        {
            if (nn.id == id)
            {
                ordered.push_back(nn);
                placed[id] = true;
                break;
            }
        }
        step = succ.find(id);
    }

    // Append anything the walk didn't reach (keeps every node).
    for (const auto& n : nodes)
        if (!placed[n.id])
            ordered.push_back(n);

    const bool fullyLinear = ordered.size() == nodes.size() && static_cast<size_t>(guard) <= nodes.size();
    nodes = std::move(ordered);
    return fullyLinear;
}

// Resolve a v2 <MidiMapping>/<OscMapping>'s pluginId + parameter to a v3 target,
// keyed through the importer's v2-uid -> v3-NodeId map. Returns nullopt (with a
// reason) when the target cannot be represented in v3: an unknown plugin id, the
// graph-IO endpoint, or the v2 bypass-toggle sentinel (parameter == -1, which v3's
// MidiTargetKind has no equivalent for yet).
std::optional<control::MidiTarget> resolveMappingTarget(const juce::XmlElement& mappingEl,
                                                        const std::unordered_map<int, NodeId>& v2ToV3,
                                                        juce::String& dropReason)
{
    const int pluginId = mappingEl.getIntAttribute("pluginId");
    const auto it = v2ToV3.find(pluginId);
    if (it == v2ToV3.end())
    {
        dropReason = "Dropped a mapping referencing an unknown plugin.";
        return std::nullopt;
    }
    if (it->second == graphIO)
    {
        dropReason = "Dropped a mapping targeting the graph I/O endpoint.";
        return std::nullopt;
    }

    const int param = mappingEl.getIntAttribute("parameter");
    if (param == -1)
    {
        dropReason = "Dropped a mapping for a plugin bypass toggle (not yet supported in v3).";
        return std::nullopt;
    }

    control::MidiTarget t;
    t.kind = control::MidiTargetKind::PluginParameter;
    t.node = it->second;
    t.parameterIndex = param;
    return t;
}

} // namespace

bool isPedalboard2Patch(const juce::XmlElement& root)
{
    return root.hasTagName("Pedalboard2PatchFile");
}

// The per-<Patch> transform shared by the single-patch and bank entry points.
static ImportResult importOnePatch(const juce::XmlElement& patchEl)
{
    ImportResult result;

    auto* graphEl = patchEl.getChildByName("FILTERGRAPH");
    if (graphEl == nullptr)
    {
        result.error = "Pedalboard2 patch has no <FILTERGRAPH>";
        return result;
    }

    auto patch = std::make_unique<Patch>();
    patch->name = patchEl.getStringAttribute("name", "Imported");

    // First pass: filters -> nodes (or graphIO for internal I/O). Build the
    // v2-uid -> v3-NodeId map used to translate connections.
    std::unordered_map<int, NodeId> v2ToV3; // v2 FILTER uid -> v3 NodeId (or graphIO)
    NodeId nextId = 1;
    bool droppedState = false;

    for (auto* filterEl : graphEl->getChildIterator())
    {
        if (!filterEl->hasTagName("FILTER"))
            continue;

        const int v2uid = filterEl->getIntAttribute("uid");
        auto* pluginEl = filterEl->getChildByName("PLUGIN");
        if (pluginEl == nullptr)
            continue;

        if (isInternalIoPlugin(*pluginEl))
        {
            v2ToV3[v2uid] = graphIO; // both Audio In and Out collapse to the endpoint
            continue;
        }

        PatchNode node;
        node.id = nextId++;
        node.desc = descFromV2Plugin(*pluginEl);
        // STATE intentionally not migrated (VST2 -> VST3 format mismatch).
        if (filterEl->getChildByName("STATE") != nullptr)
            droppedState = true;

        v2ToV3[v2uid] = node.id;
        patch->nodes.push_back(std::move(node));
    }

    if (droppedState)
        result.warnings.add("Plugin settings (state) were not migrated across the VST2 -> VST3 format change.");

    // Second pass: connections.
    for (auto* connEl : graphEl->getChildIterator())
    {
        if (!connEl->hasTagName("CONNECTION"))
            continue;

        const int srcUid = connEl->getIntAttribute("srcFilter");
        const int dstUid = connEl->getIntAttribute("dstFilter");
        const auto srcIt = v2ToV3.find(srcUid);
        const auto dstIt = v2ToV3.find(dstUid);
        if (srcIt == v2ToV3.end() || dstIt == v2ToV3.end())
        {
            result.warnings.add("Dropped a connection referencing an unknown filter.");
            continue;
        }

        PatchConnection c;
        c.src = srcIt->second;
        c.srcChannel = connEl->getIntAttribute("srcChannel");
        c.dst = dstIt->second;
        c.dstChannel = connEl->getIntAttribute("dstChannel");
        patch->connections.push_back(c);
    }

    // Custom names: <UserNames><Name id val/>.
    if (auto* userNames = patchEl.getChildByName("UserNames"))
    {
        for (auto* nameEl : userNames->getChildIterator())
        {
            if (!nameEl->hasTagName("Name"))
                continue;
            const int v2uid = nameEl->getIntAttribute("id");
            const auto it = v2ToV3.find(v2uid);
            if (it == v2ToV3.end())
                continue;
            if (auto* pn = patch->findNode(it->second))
                pn->customName = nameEl->getStringAttribute("val");
        }
    }

    // Mappings: <Mappings> is a child of <Patch> (sibling of <FILTERGRAPH>), holding
    // <MidiMapping>/<OscMapping> entries. Translate each to a v3 per-patch mapping,
    // resolving its plugin id through v2ToV3 (now fully built). Per-patch only — v2's
    // app-command mappings live in a separate global file, never here.
    if (auto* mappingsEl = patchEl.getChildByName("Mappings"))
    {
        for (auto* el : mappingsEl->getChildIterator())
        {
            const bool isMidi = el->hasTagName("MidiMapping");
            const bool isOsc = el->hasTagName("OscMapping");
            if (!isMidi && !isOsc)
                continue;

            juce::String dropReason;
            auto target = resolveMappingTarget(*el, v2ToV3, dropReason);
            if (!target)
            {
                result.warnings.add(dropReason);
                continue;
            }

            if (isMidi)
            {
                control::MidiMapping m;
                m.source.kind = control::MidiSourceKind::ControlChange; // v2 MidiMapping is always CC
                m.source.channel = el->getIntAttribute("channel", 0);   // 0 == omni, matches v3
                m.source.controller = el->getIntAttribute("cc", 0);
                m.rangeMin = static_cast<float>(el->getDoubleAttribute("lowerBound", 0.0));
                // Default 1.0 (vs v2's bare 0.0) only matters for a hand-edited file missing
                // the attribute — v2 always writes both bounds; 1.0 avoids a degenerate 0..0 range.
                m.rangeMax = static_cast<float>(el->getDoubleAttribute("upperBound", 1.0));
                m.inverted = false; // v2 inversion rides a swapped range; scaleToRange honours min>max
                m.latch = el->getBoolAttribute("latch", false);
                m.target = *target;
                patch->midiMappings.push_back(std::move(m));
            }
            else
            {
                control::OscMapping o;
                o.address = el->getStringAttribute("address");
                o.argumentIndex = el->getIntAttribute("parameterIndex", 0); // v2 OSC arg index (not the plugin param)
                o.target = *target; // rangeMin/rangeMax/inverted keep v3 defaults (v2 OSC has no range)
                patch->oscMappings.push_back(std::move(o));
            }
        }
    }

    if (!orderNodesBySignalFlow(patch->nodes, patch->connections))
        result.warnings.add("Patch is not a simple linear chain; node order is approximate.");

    result.patch = std::move(patch);
    return result;
}

ImportResult importPedalboard2Patch(const juce::XmlElement& root)
{
    ImportResult result;

    if (!isPedalboard2Patch(root))
    {
        result.error = "Not a Pedalboard2 patch file";
        return result;
    }

    auto* patchEl = root.getChildByName("Patch");
    if (patchEl == nullptr)
    {
        result.error = "Pedalboard2 file has no <Patch>";
        return result;
    }

    result = importOnePatch(*patchEl);
    if (result.patch != nullptr && patchEl->getNextElementWithTagName("Patch") != nullptr)
        result.warnings.add("File contains multiple patches; only the first was imported.");
    return result;
}

BankImportResult importPedalboard2Bank(const juce::XmlElement& root)
{
    BankImportResult result;

    if (!isPedalboard2Patch(root))
    {
        result.error = "Not a Pedalboard2 patch file";
        return result;
    }

    int patchCount = 0;
    for (auto* el : root.getChildWithTagNameIterator("Patch"))
    {
        juce::ignoreUnused(el);
        ++patchCount;
    }
    if (patchCount == 0)
    {
        result.error = "Pedalboard2 file has no <Patch>";
        return result;
    }

    auto bank = std::make_unique<PatchBank>();
    bank->patches.clear();

    for (auto* patchEl : root.getChildWithTagNameIterator("Patch"))
    {
        const int position = static_cast<int>(bank->patches.size()) + 1;
        auto one = importOnePatch(*patchEl);
        if (one.patch == nullptr)
        {
            result.error = "Patch " + juce::String(position) + ": " + one.error;
            return result;
        }

        // Name-prefix warnings only when there is more than one patch to tell apart.
        for (const auto& w : one.warnings)
            result.warnings.add(patchCount > 1 ? ("Patch \"" + one.patch->name + "\": " + w) : w);

        bank->patches.push_back(std::move(*one.patch));
    }

    bank->activeIndex = 0;
    result.bank = std::move(bank);
    return result;
}

} // namespace pedalboard3::session::v2
