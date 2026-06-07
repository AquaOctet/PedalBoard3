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

        if (auto descXml = n.desc.createXml())
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

    return root;
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

            if (auto* identityEl = nodeEl->getChildByName("IDENTITY"))
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

    return patch;
}

} // namespace pedalboard3::session
