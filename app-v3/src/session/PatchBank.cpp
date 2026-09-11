#include "PatchBank.h"

namespace pedalboard3::session
{

int PatchBank::add(Patch patch)
{
    patches.push_back(std::move(patch));
    return size() - 1;
}

int PatchBank::duplicate(int i)
{
    if (!isValidIndex(i))
        return -1;

    // Patch is a plain aggregate of vectors + juce::String/MemoryBlock, so a
    // copy is already deep — no shared state between original and copy.
    Patch copy = patches[static_cast<size_t>(i)];
    copy.name = copy.name + " copy";

    const int newIndex = i + 1;
    patches.insert(patches.begin() + newIndex, std::move(copy));
    if (activeIndex >= newIndex)
        ++activeIndex;
    return newIndex;
}

bool PatchBank::rename(int i, juce::String name)
{
    if (!isValidIndex(i))
        return false;
    name = name.trim();
    patches[static_cast<size_t>(i)].name = name.isEmpty() ? juce::String("Untitled") : name;
    return true;
}

bool PatchBank::remove(int i)
{
    if (!isValidIndex(i))
        return false;

    if (size() == 1)
    {
        patches = {Patch{}};
        activeIndex = 0;
        return true;
    }

    patches.erase(patches.begin() + i);
    if (activeIndex > i)
        --activeIndex;
    if (activeIndex >= size())
        activeIndex = size() - 1;
    return true;
}

bool PatchBank::select(int i) noexcept
{
    if (!isValidIndex(i))
        return false;
    activeIndex = i;
    return true;
}

void PatchBank::next() noexcept
{
    activeIndex = (activeIndex + 1) % size();
}

void PatchBank::previous() noexcept
{
    activeIndex = (activeIndex + size() - 1) % size();
}

} // namespace pedalboard3::session
