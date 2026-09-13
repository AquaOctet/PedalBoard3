#pragma once

#include "Features.h"
#include "Patch.h"

#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <vector>

namespace pedalboard3::session
{

// An ordered bank of patches with one active index — the document a .pdl
// holds from schemaMinor 4 on (roadmap M3, plan
// docs/plans/2026-09-11-001-feat-multi-patch-banks-plan.md, KD1).
//
// Pure value type: no knowledge of Session or the live graph. Switching the
// live patch is BankDocument's job (U3); this type only keeps the list and
// the index consistent. Invariants after every operation:
//   - patches is never empty (removing the last patch leaves one empty
//     "Untitled" patch);
//   - 0 <= activeIndex < patches.size().
// A default-constructed bank satisfies both (one Untitled patch, index 0).
struct PatchBank
{
    std::vector<Patch> patches{Patch{}};
    int activeIndex{0};

    // The board's switches for the optional features (plan
    // docs/plans/2026-09-12-002-feat-optional-features-plan.md, KD3): on unless
    // the board turns one off. Saved as <BANK> attributes, only when off.
    std::array<bool, kFeatureCount> features{true, true, true, true, true};

    bool featureOn(Feature feature) const noexcept { return features[indexOf(feature)]; }
    void setFeature(Feature feature, bool on) noexcept { features[indexOf(feature)] = on; }

    int size() const noexcept { return static_cast<int>(patches.size()); }
    bool isValidIndex(int i) const noexcept { return i >= 0 && i < size(); }

    Patch& active() noexcept { return patches[static_cast<size_t>(activeIndex)]; }
    const Patch& active() const noexcept { return patches[static_cast<size_t>(activeIndex)]; }

    // Append and return the new index. Does not change activeIndex.
    int add(Patch patch);

    // Deep-copy patches[i] (nodes, state blobs, connections, mappings) right
    // after it, named "<name> copy". Returns the new index, or -1 if `i` is
    // out of range. Does not change activeIndex, except to keep pointing at
    // the same patch when the copy is inserted before it.
    int duplicate(int i);

    // Rename patches[i]. An empty name becomes "Untitled". Returns false if
    // `i` is out of range.
    bool rename(int i, juce::String name);

    // Remove patches[i]. Removing the only patch resets the bank to one empty
    // Untitled patch. activeIndex follows the active patch when a lower index
    // is removed; removing the active patch itself activates the patch that
    // now occupies its slot (or the new last one). Returns false if out of range.
    bool remove(int i);

    // Set activeIndex. Returns false (no change) if out of range.
    bool select(int i) noexcept;

    // Step activeIndex with wrap-around.
    void next() noexcept;
    void previous() noexcept;
};

// PatchBank <-> XML: a <BANK activeIndex="n"> element holding one <PATCH>
// child per patch (patchToXml / patchFromXml), plus tempo="0" and so on for
// each board feature switch that's off (an absent one is on). Shared by Preferences
// (auto-restore) and available to any other store. bankFromXml returns
// nullptr if the element is not a <BANK>, holds no <PATCH>, or any patch
// fails to parse or validatePatch; an out-of-range activeIndex clamps to 0.
std::unique_ptr<juce::XmlElement> bankToXml(const PatchBank&);
std::unique_ptr<PatchBank> bankFromXml(const juce::XmlElement&);

// The board's feature switches as <BANK> attributes (optional-features U1):
// written only when off, read as on when absent. bankToXml / bankFromXml and
// the .pdl writer and reader all use these.
void writeFeatureAttributes(const PatchBank& bank, juce::XmlElement& bankElement);
void readFeatureAttributes(PatchBank& bank, const juce::XmlElement& bankElement);

} // namespace pedalboard3::session
