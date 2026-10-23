// Tests for ui::releaseEditorFromProcessors -- EditorWindow's half of JUCE's
// host contract. The window owns the plugin editor, so the window is what
// releases it from the processors before deleting it; the release itself is a
// free function so it can be checked without a desktop window.
//
// ~AudioProcessorEditor does not notify its processor, it only asserts that
// the host already did. Until JUCE 8.0.16 a missed release was invisible,
// because createEditorIfNeeded handed the freed editor back to the next
// caller; 8.0.16's createEditorAndMakeActive refuses to build a second editor
// while one is still registered, which turns the same mistake into a plugin
// window that opens once and never again.

#include <catch2/catch_test_macros.hpp>

#include "engine/BypassableInstance.h"
#include "support/TestGainProcessor.h"
#include "ui/EditorWindow.h"

#include <memory>

using namespace pedalboard3;
using namespace pedalboard3::testing;

namespace
{
struct TinyEditor : juce::AudioProcessorEditor
{
    explicit TinyEditor(juce::AudioProcessor& p) : juce::AudioProcessorEditor(p) { setSize(120, 80); }
};

// TestGainProcessor has no editor; this one really hands one out.
class EditorGainProcessor : public TestGainProcessor
{
public:
    bool hasEditor() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override
    {
        ++editorsCreated;
        return new TinyEditor(*this);
    }

    int editorsCreated = 0;
};

// What SignalCanvas::onCardEdit and ~EditorWindow do between them: ask the
// processor the canvas holds for an editor, then release and delete it.
void openAndClose(juce::AudioProcessor& host)
{
    auto* editor = host.createEditorAndMakeActive();
    REQUIRE(editor != nullptr);
    ui::releaseEditorFromProcessors(host, editor);
    delete editor;
}
} // namespace

TEST_CASE("EditorWindow: a hosted plugin's editor can be opened again after the window closes", "[ui][editor-window]")
{
    juce::MessageManager::getInstance(); // createEditorAndMakeActive wants the message thread

    auto innerOwned = std::make_unique<EditorGainProcessor>();
    auto* inner = innerOwned.get();
    engine::BypassableInstance wrapper(std::move(innerOwned));
    REQUIRE(wrapper.hasEditor());

    openAndClose(wrapper);
    REQUIRE(inner->editorsCreated == 1);

    // Both slots have to be clear, not only the one the editor points at: the
    // wrapper is what the canvas asks next time.
    REQUIRE(wrapper.getActiveEditor() == nullptr);
    REQUIRE(inner->getActiveEditor() == nullptr);

    openAndClose(wrapper);
    REQUIRE(inner->editorsCreated == 2);
    REQUIRE(wrapper.getActiveEditor() == nullptr);
    REQUIRE(inner->getActiveEditor() == nullptr);
}

TEST_CASE("EditorWindow: a built-in processor's editor is released from its one processor", "[ui][editor-window]")
{
    juce::MessageManager::getInstance();

    // No wrapper in between, so the host processor and the editor's own
    // processor are the same object and the second release finds nothing to do.
    EditorGainProcessor processor;

    openAndClose(processor);
    REQUIRE(processor.editorsCreated == 1);
    REQUIRE(processor.getActiveEditor() == nullptr);

    openAndClose(processor);
    REQUIRE(processor.editorsCreated == 2);
    REQUIRE(processor.getActiveEditor() == nullptr);
}

TEST_CASE("EditorWindow: releasing a null editor is a no-op", "[ui][editor-window]")
{
    EditorGainProcessor processor;
    ui::releaseEditorFromProcessors(processor, nullptr); // a window with no content component
    REQUIRE(processor.getActiveEditor() == nullptr);
}
