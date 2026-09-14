#pragma once

#include "AppContext.h"
#include "session/PatchBank.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace pedalboard3::ui
{

// The board's name in the title bar and prompts: the file name without its
// extension, or "Untitled".
juce::String boardDisplayName(const juce::File& file);

// The File workflow for the bank document (finish pass U3-U5, plan
// docs/plans/2026-09-11-003-feat-finish-pass-core-plan.md): New, Open, Save,
// Save As, Save as Default and Reset Default, plus the save-before-discard
// prompt (KD4) that New, Open and quit share. Owned by MainWindow's root
// component; the File menu, the file.save command and quit call into it.
//
// Threading: message thread only. Every dialog is async; callbacks hold a
// WeakReference so none runs after this object is gone.
class FileActions
{
public:
    explicit FileActions(AppContext& ctx);
    ~FileActions();

    // Prompt if unsaved, then start from the default bank (or one empty patch),
    // untitled.
    void newBank();

    // Prompt if unsaved, then pick a .pdl (v3, or a Pedalboard2 file to import).
    void open();

    // Write to the current file, or ask for one when the board is untitled.
    // `done` gets whether the bank was written (false on cancel or error). A
    // board a newer version wrote (read-only, M5 U4) is never saved over: Save
    // goes to Save As, which warns before writing a copy.
    void save(std::function<void(bool saved)> done = {});
    void saveAs(std::function<void(bool saved)> done = {});

    // Save the board as the one New starts from; Reset Default deletes it.
    void saveAsDefault();
    void resetDefault();
    bool hasDefault() const;

    // File > Open Recent (#30): the banks last opened or saved, newest first.
    // Opening one prompts if unsaved (KD4); one that has gone is dropped from
    // the list, with a message.
    juce::Array<juce::File> recentFiles() const;
    void openRecent(const juce::File& file);
    void clearRecentFiles();

    // A .pdl dropped on the window (#31): prompt if unsaved, then open it as
    // File > Open does (a v3 bank, or a Pedalboard2 file to import).
    void openDropped(const juce::File& file);

    // KD4. With unsaved changes, ask Save / Don't Save / Cancel before
    // `beforeWhat` (e.g. "quitting"). `proceed` runs unless the user cancels
    // or the save doesn't happen; `discarded` is true after Don't Save.
    // Without unsaved changes `proceed(false)` runs straight away.
    void confirmDiscard(const juce::String& beforeWhat, std::function<void(bool discarded)> proceed);

private:
    void chooseFileToOpen();
    void openFile(const juce::File& file);
    void loadIntoDocument(session::PatchBank bank, const juce::File& file, bool readOnly = false);
    void chooseSaveFile(std::function<void(bool saved)> done);
    void writeTo(const juce::File& file, const std::function<void(bool)>& done);
    void promptThenOpen(const juce::File& file, const juce::String& beforeWhat);
    void rememberRecent(const juce::File& file);

    AppContext& ctx_;
    // Held as members so they outlive their async callbacks.
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Component::SafePointer<juce::Component> prompt_; // the Unsaved changes alert while it's open

    JUCE_DECLARE_WEAK_REFERENCEABLE(FileActions)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileActions)
};

} // namespace pedalboard3::ui
