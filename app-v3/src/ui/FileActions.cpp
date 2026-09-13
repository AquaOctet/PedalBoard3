#include "FileActions.h"
#include "session/SafeXml.h"

#include "engine/PluginHost.h"
#include "session/BankDocument.h"
#include "session/BoardFiles.h"
#include "session/DefaultBank.h"
#include "session/PdlSerializer.h"
#include "session/Preferences.h"
#include "session/PreparedGraph.h"
#include "session/v2_import/PdlV2Importer.h"

#include <vector>

namespace pedalboard3::ui
{

namespace
{

juce::File documentsFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Pedalboard 3");
}

void showMessage(juce::MessageBoxIconType icon, const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions{}.withIconType(icon).withTitle(title).withMessage(message).withButton("OK"), nullptr);
}

// Resolve an imported (e.g. Pedalboard2/VST2) plugin descriptor to an installed
// plugin by display name — case-insensitive, effects preferred over instruments.
// Returns nullptr when nothing matches. Lets a v2 import reconnect to the user's
// installed VST3/AU build of the same plugin even though the format differs.
const juce::PluginDescription* findKnownByName(const juce::Array<juce::PluginDescription>& known,
                                               const juce::String& name)
{
    const juce::PluginDescription* best = nullptr;
    for (const auto& d : known)
    {
        if (!d.name.equalsIgnoreCase(name))
            continue;
        if (!d.isInstrument)
            return &d; // an effect match is exactly what we want
        if (best == nullptr)
            best = &d; // remember an instrument match as a last resort
    }
    return best;
}

struct LoadedBank
{
    std::unique_ptr<session::PatchBank> bank;
    bool imported{false}; // a Pedalboard2 file: the board is untitled until saved as .pdl v3
    bool readOnly{false}; // saved by a newer major version (M5 U4)
    juce::StringArray warnings;
    juce::String error;
};

// Load a patch BANK for File > Open, transparently handling both v3 .pdl and
// legacy Pedalboard2 (.pdl v2) files. For a v2 file it imports every patch's
// topology and resolves each plugin to an installed one by name (dropping
// unmatched, with a warning); state is not migrated across the VST2 -> VST3
// format change.
LoadedBank loadOpenedBank(const juce::File& file, engine::PluginHost& host)
{
    LoadedBank out;

    // Parsed under the same rules as a v3 board (M5 U1): capped, no DTDs or entities.
    const auto parsed = session::safexml::parseFile(file);
    const auto& xml = parsed.xml;
    if (xml != nullptr && session::v2::isPedalboard2Patch(*xml))
    {
        auto imp = session::v2::importPedalboard2Bank(*xml);
        out.imported = true;
        out.warnings = imp.warnings;
        out.error = imp.error;
        if (imp.bank == nullptr)
            return out;

        const auto known = host.getKnownPlugins();
        for (auto& patch : imp.bank->patches)
        {
            std::vector<session::PatchNode> kept;
            for (auto& node : patch.nodes)
            {
                if (const auto* match = findKnownByName(known, node.desc.name))
                {
                    node.desc = *match;    // resolve to the installed plugin
                    node.state.setSize(0); // cross-format; state already not migrated
                    kept.push_back(std::move(node));
                }
                else
                {
                    out.warnings.add("Skipped \"" + node.desc.name +
                                     "\" - not found in your scanned plugins (scan it, or no installed equivalent).");
                }
            }
            patch.nodes = std::move(kept);
            patch.connections = session::linearChainConnections(patch.nodes);
        }
        out.bank = std::move(imp.bank);
        return out;
    }

    auto loaded = session::pdl::readBank(file);
    out.bank = std::move(loaded.bank);
    out.error = loaded.error;
    out.readOnly = loaded.readOnly;
    return out;
}
} // namespace

juce::String boardDisplayName(const juce::File& file)
{
    return file == juce::File() ? juce::String("Untitled") : file.getFileNameWithoutExtension();
}

FileActions::FileActions(AppContext& ctx) : ctx_(ctx) {}

FileActions::~FileActions() = default;

void FileActions::newBank()
{
    juce::WeakReference<FileActions> weak(this);
    confirmDiscard("starting a new board",
                   [weak](bool)
                   {
                       if (weak != nullptr)
                           weak->loadIntoDocument(
                               session::newBoardBank(session::defaultBankFile(weak->ctx_.preferences)), {});
                   });
}

void FileActions::open()
{
    juce::WeakReference<FileActions> weak(this);
    confirmDiscard("opening another board",
                   [weak](bool)
                   {
                       if (weak != nullptr)
                           weak->chooseFileToOpen();
                   });
}

void FileActions::chooseFileToOpen()
{
    chooser_ = std::make_unique<juce::FileChooser>("Open Pedalboard", documentsFolder(), "*.pdl");
    juce::WeakReference<FileActions> weak(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [weak](const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (weak == nullptr || file == juce::File())
                                  return; // cancelled
                              weak->openFile(file);
                          });
}

void FileActions::openFile(const juce::File& file)
{
    // Handles both v3 .pdl and legacy Pedalboard2 (.pdl v2) files.
    auto loaded = loadOpenedBank(file, ctx_.pluginHost);
    if (loaded.bank == nullptr)
    {
        showMessage(juce::MessageBoxIconType::WarningIcon,
                    "Couldn't open patch",
                    loaded.error.isNotEmpty() ? loaded.error : juce::String("Couldn't read the patch file."));
        return;
    }

    // A Pedalboard2 import stays untitled until it is saved as a v3 .pdl.
    loadIntoDocument(std::move(*loaded.bank), loaded.imported ? juce::File{} : file, loaded.readOnly);
    rememberRecent(file);

    // Surface import notes (Pedalboard2 conversion: dropped state, skipped
    // plugins, etc.) once the chain is in.
    if (!loaded.warnings.isEmpty())
        showMessage(juce::MessageBoxIconType::InfoIcon,
                    "Patch imported",
                    "Imported with notes:\n\n - " + loaded.warnings.joinIntoString("\n - "));
}

void FileActions::loadIntoDocument(session::PatchBank bank, const juce::File& file, bool readOnly)
{
    // Every slot is one switch away from live, so before any patch can go live
    // its plugins are found in the scanned list by ID (M5 U3: one that isn't
    // there becomes a pass-through placeholder whose card says Missing), then
    // what can't be built at all goes (see session::dropUnsafeNodes). Editors
    // close through BankDocument::beforeSwap before the swap destroys the live
    // instances, and TransportBar clears Test Tone when the swap re-enables live
    // capture.
    session::resolvePluginNodes(bank, ctx_.pluginHost);
    session::dropUnsafeNodes(bank, ctx_.pluginHost);
    ctx_.bankDocument.loadBank(std::move(bank), file, readOnly);
}

void FileActions::save(std::function<void(bool saved)> done)
{
    // A board a newer version wrote is never saved over (M5 U4): Save goes to
    // Save As, which warns first.
    const auto file = ctx_.bankDocument.currentFile();
    if (file == juce::File() || ctx_.bankDocument.isReadOnly())
    {
        saveAs(std::move(done));
        return;
    }
    writeTo(file, done);
}

void FileActions::saveAs(std::function<void(bool saved)> done)
{
    if (!ctx_.bankDocument.isReadOnly())
    {
        chooseSaveFile(std::move(done));
        return;
    }

    // A board a newer version wrote (M5 U4, plan KD7): this version can't keep
    // what it doesn't understand, so say so before writing a copy.
    juce::WeakReference<FileActions> weak(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions{}
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Save a copy?")
            .withMessage("This board was saved by a newer version of Pedalboard 3. The copy is saved in this "
                         "version's format, which can't hold everything the newer version understands, so some of "
                         "its settings may not come through. The original file isn't changed.")
            .withButton("Save a Copy")
            .withButton("Cancel"),
        [weak, done](int result)
        {
            if (weak == nullptr)
                return;
            if (result == 1)
                weak->chooseSaveFile(done);
            else if (done)
                done(false);
        });
}

void FileActions::chooseSaveFile(std::function<void(bool saved)> done)
{
    const auto current = ctx_.bankDocument.currentFile();
    const auto start = current != juce::File() ? current : documentsFolder().getChildFile("Untitled.pdl");
    chooser_ = std::make_unique<juce::FileChooser>("Save Pedalboard", start, "*.pdl");

    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                       juce::FileBrowserComponent::warnAboutOverwriting;

    juce::WeakReference<FileActions> weak(this);
    chooser_->launchAsync(flags,
                          [weak, done = std::move(done)](const juce::FileChooser& fc)
                          {
                              if (weak == nullptr)
                                  return;
                              auto file = fc.getResult();
                              if (file == juce::File())
                              {
                                  if (done)
                                      done(false); // cancelled
                                  return;
                              }
                              if (!file.hasFileExtension("pdl"))
                                  file = file.withFileExtension("pdl");
                              weak->writeTo(file, done);
                          });
}

void FileActions::writeTo(const juce::File& file, const std::function<void(bool)>& done)
{
    // The whole bank, the active slot refreshed with live plugin state so a
    // later load restores knob settings (master plan §4.5 / §4.2.B). Taken
    // now, not when the chooser opened, so markSaved matches what was written.
    const auto result = session::pdl::writeBank(ctx_.bankDocument.snapshotForSave(), file);
    if (result.wasOk())
    {
        ctx_.bankDocument.markSaved(file);
        rememberRecent(file);
    }
    else
        showMessage(juce::MessageBoxIconType::WarningIcon, "Couldn't save", result.getErrorMessage());

    if (done)
        done(result.wasOk());
}

void FileActions::saveAsDefault()
{
    const auto result =
        session::pdl::writeBank(ctx_.bankDocument.snapshotForSave(), session::defaultBankFile(ctx_.preferences));
    if (result.wasOk())
        showMessage(juce::MessageBoxIconType::InfoIcon, "Default saved", "File > New now starts from this board.");
    else
        showMessage(juce::MessageBoxIconType::WarningIcon, "Couldn't save the default", result.getErrorMessage());
}

void FileActions::resetDefault()
{
    const auto file = session::defaultBankFile(ctx_.preferences);
    if (!file.existsAsFile() || file.deleteFile())
        showMessage(juce::MessageBoxIconType::InfoIcon, "Default reset", "File > New now starts with one empty patch.");
    else
        showMessage(juce::MessageBoxIconType::WarningIcon,
                    "Couldn't reset the default",
                    "Couldn't delete " + file.getFullPathName());
}

bool FileActions::hasDefault() const
{
    return session::defaultBankFile(ctx_.preferences).existsAsFile();
}

void FileActions::confirmDiscard(const juce::String& beforeWhat, std::function<void(bool discarded)> proceed)
{
    if (!ctx_.bankDocument.isModified())
    {
        proceed(false);
        return;
    }
    if (prompt_ != nullptr)
    {
        prompt_->toFront(true); // already asking
        return;
    }

    prompt_ = std::make_unique<juce::AlertWindow>("Unsaved changes",
                                                  "Save the changes to \"" +
                                                      boardDisplayName(ctx_.bankDocument.currentFile()) + "\" before " +
                                                      beforeWhat + "? Changes you don't save are lost.",
                                                  juce::MessageBoxIconType::QuestionIcon);
    prompt_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt_->addButton("Don't Save", 2);
    prompt_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::WeakReference<FileActions> weak(this);
    prompt_->enterModalState(true,
                             juce::ModalCallbackFunction::create(
                                 [weak, proceed = std::move(proceed)](int result)
                                 {
                                     if (weak == nullptr)
                                         return;
                                     weak->prompt_.reset();
                                     if (result == 1)
                                         weak->save(
                                             [proceed](bool saved)
                                             {
                                                 if (saved)
                                                     proceed(false);
                                             });
                                     else if (result == 2)
                                         proceed(true);
                                 }),
                             false);
}

juce::Array<juce::File> FileActions::recentFiles() const
{
    juce::Array<juce::File> recent;
    for (const auto& path : ctx_.preferences.getRecentFiles())
        if (juce::File::isAbsolutePath(path))
            recent.add(juce::File(path));
    return recent;
}

void FileActions::openRecent(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        ctx_.preferences.setRecentFiles(session::boardfiles::removeRecent(ctx_.preferences.getRecentFiles(), file));
        showMessage(juce::MessageBoxIconType::WarningIcon,
                    "Couldn't open patch",
                    "\"" + file.getFullPathName() +
                        "\" isn't there any more, so it has been removed from Open Recent.");
        return;
    }
    promptThenOpen(file, "opening another board");
}

void FileActions::clearRecentFiles()
{
    ctx_.preferences.setRecentFiles({});
}

void FileActions::openDropped(const juce::File& file)
{
    promptThenOpen(file, "opening \"" + boardDisplayName(file) + "\"");
}

void FileActions::promptThenOpen(const juce::File& file, const juce::String& beforeWhat)
{
    juce::WeakReference<FileActions> weak(this);
    confirmDiscard(beforeWhat,
                   [weak, file](bool)
                   {
                       if (weak != nullptr)
                           weak->openFile(file);
                   });
}

// A bank opened (including a Pedalboard2 file, which reopens as an import) or
// saved goes to the front of Open Recent.
void FileActions::rememberRecent(const juce::File& file)
{
    ctx_.preferences.setRecentFiles(session::boardfiles::pushRecent(ctx_.preferences.getRecentFiles(), file));
}

} // namespace pedalboard3::ui
