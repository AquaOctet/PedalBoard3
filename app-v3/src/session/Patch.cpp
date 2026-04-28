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

} // namespace pedalboard3::session
