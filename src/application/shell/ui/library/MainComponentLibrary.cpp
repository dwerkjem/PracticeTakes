#include "../../../localisation/Localisation.h"
#include "../../MainComponent.h"

#include "../score/BundledSampleScore.h"

#include "platform/library/LibraryLayout.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

#include <memory>
#include <utility>

// The shell's half of the media library (openspec/changes/media-library): where
// the library is, saving takes into it, the Library window and its commands.
//
// The disk is read on `libraryWork`'s thread and takes are written on
// `takeSaver`'s; what each finds comes back here, on the message thread, to be
// shown. Nothing here blocks on a listing or a take.

namespace
{
using Safe = juce::Component::SafePointer<MainComponent>;

// Song files the chooser offers, as Open Score does.
constexpr const char* songPatterns = "*.musicxml;*.xml;*.mxl";
} // namespace

library::Layout MainComponent::libraryLayout() const
{
    if (scratchLibraryFolder != juce::File())
    {
        return {scratchLibraryFolder};
    }
    if (libraryFolderSetting.isNotEmpty() && juce::File::isAbsolutePath(libraryFolderSetting))
    {
        return {juce::File(libraryFolderSetting)};
    }
    return {library::defaultFolder()};
}

bool MainComponent::libraryReachable() const
{
    // Not there yet is fine -- the first song or take makes it -- so long as
    // where it would go exists. A folder on a drive that is not attached has
    // no parent either.
    const juce::File root = libraryLayout().root;
    return root.isDirectory() || root.getParentDirectory().isDirectory();
}

void MainComponent::useScratchLibrary()
{
    scratchLibraryFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getNonexistentChildFile("practice-takes-capture-library", "");
    static_cast<void>(scratchLibraryFolder.createDirectory());
}

juce::File MainComponent::intoLibrary(const juce::File& file)
{
    if (file == juce::File() || file.isAChildOf(bundled_sample::defaultFolder()))
    {
        return file;
    }
    juce::File copy = library::intoLibrary(libraryLayout(), file);
    if (copy == juce::File())
    {
        // Opened anyway: failing to copy is no reason not to open.
        showLibraryStatus(l10n::format(
            "library.song.copy-failed",
            {{"file", file.getFileName()}, {"folder", libraryLayout().songs().getFullPathName()}}));
        return file;
    }
    if (libraryWindow != nullptr)
    {
        refreshLibrary();
    }
    return copy;
}

void MainComponent::showLibrary()
{
    if (libraryWindow == nullptr)
    {
        const Safe safeThis(this);
        LibraryWindow::Commands commands;
        commands.openSong = [safeThis](const juce::File& file)
        {
            if (safeThis != nullptr)
            {
                safeThis->startScoreImport(file);
            }
        };
        commands.addSongs = [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->addSongsToLibrary();
            }
        };
        commands.removeSong = [safeThis](const library::SongEntry& song)
        {
            if (safeThis != nullptr)
            {
                safeThis->removeLibrarySong(song);
            }
        };
        commands.reviewTake = [safeThis](const juce::File& folder)
        {
            if (safeThis != nullptr)
            {
                safeThis->reviewLibraryTake(folder);
            }
        };
        commands.playTake = [safeThis](const juce::File& folder)
        {
            if (safeThis != nullptr)
            {
                safeThis->playLibraryTake(folder);
            }
        };
        commands.exportTake = [safeThis](const juce::File& folder)
        {
            if (safeThis != nullptr)
            {
                safeThis->exportLibraryTakeAudio(folder);
            }
        };
        commands.deleteTake = [safeThis](const library::TakeEntry& entry)
        {
            if (safeThis != nullptr)
            {
                safeThis->deleteLibraryTake(entry);
            }
        };
        commands.showInFolder = [](const juce::File& file)
        {
            if (file.exists())
            {
                file.revealToUser();
            }
        };
        commands.changeFolder = [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->changeLibraryFolder();
            }
        };
        commands.refresh = [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->refreshLibrary();
            }
        };
        commands.close = [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->closeLibrary();
            }
        };
        libraryWindow = std::make_unique<LibraryWindow>(std::move(commands));
        libraryWindow->applyAppearance(&appLookAndFeel, appPaletteFor(currentTheme).background);
        libraryWindow->showStatus(libraryStatus);
    }
    libraryWindow->setVisible(true);
    libraryWindow->toFront(true);
    refreshLibrary();
}

void MainComponent::closeLibrary()
{
    libraryWindow.reset();
}

void MainComponent::refreshLibrary()
{
    if (libraryWindow == nullptr)
    {
        return;
    }
    const library::Layout layout = libraryLayout();
    const bool reachable = libraryReachable();
    libraryWindow->showFolder(layout.root, reachable);
    if (!reachable)
    {
        libraryWindow->showSongs({});
        libraryWindow->showTakes({});
        return;
    }

    // The sample is always in the library, so it is written out if a first
    // launch has not done it yet (a few kilobytes, as openSampleScore says).
    static_cast<void>(bundled_sample::installBundledSampleScore());
    const juce::File samples = bundled_sample::defaultFolder();
    const Safe safeThis(this);
    libraryWork.post(
        [layout, samples, safeThis]() -> library::BackgroundWork::Finish
        {
            auto songs = std::make_shared<std::vector<library::SongEntry>>(
                library::listSongs(layout, samples));
            auto takes =
                std::make_shared<std::vector<library::TakeEntry>>(library::listTakes(layout));
            return [safeThis, songs, takes]
            {
                if (safeThis != nullptr && safeThis->libraryWindow != nullptr)
                {
                    safeThis->libraryWindow->showSongs(std::move(*songs));
                    safeThis->libraryWindow->showTakes(std::move(*takes));
                }
            };
        });
}

void MainComponent::showLibraryStatus(const juce::String& status)
{
    libraryStatus = status;
    if (libraryWindow != nullptr)
    {
        libraryWindow->showStatus(status);
    }
}

void MainComponent::offerTakesToLibrary()
{
    takeSaver.offer(libraryLayout(), takeRecorder.takes());
}

void MainComponent::takeSavedToLibrary(const library::TakeSaver::Saved& saved)
{
    if (saved.error.empty())
    {
        showLibraryStatus(
            l10n::format("library.take.saved", {{"folder", saved.folder.getFileName()}}));
        refreshLibrary();
        return;
    }

    const juce::String problem = l10n::format(
        "library.take.save-failed", {{"reason", l10n::text(saved.error)},
                                     {"folder", libraryLayout().takes().getFullPathName()}});
    showLibraryStatus(problem);
    // Said once a session out loud, not only in a window that may be closed:
    // a take that is not saved is a recording that is gone when the
    // application quits. After that, the status line keeps saying it.
    if (!libraryProblemAlerted && scratchLibraryFolder == juce::File())
    {
        libraryProblemAlerted = true;
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, l10n::text("library.take.save-failed.title"),
            problem);
    }
}

void MainComponent::reviewLibraryTake(const juce::File& folder)
{
    if (takeRecorder.isRecording())
    {
        showLibraryStatus(l10n::text("library.take.busy"));
        playLoadedTake = false;
        return;
    }
    showLibraryStatus(l10n::text("library.take.loading"));
    const Safe safeThis(this);
    libraryWork.post(
        [folder, safeThis]() -> library::BackgroundWork::Finish
        {
            auto loaded = std::make_shared<library::LoadedTake>(library::readTake(folder));
            return [safeThis, loaded]
            {
                if (safeThis != nullptr)
                {
                    safeThis->finishLoadingLibraryTake(*loaded);
                }
            };
        });
}

void MainComponent::finishLoadingLibraryTake(const library::LoadedTake& loaded)
{
    if (loaded.take == nullptr)
    {
        playLoadedTake = false;
        showLibraryStatus(l10n::text(loaded.error));
        return;
    }
    if (takeRecorder.isRecording())
    {
        showLibraryStatus(l10n::text("library.take.busy"));
        playLoadedTake = false;
        return;
    }

    // The take's own score becomes the open one, so the Score View shows what
    // it was played against (decision 5). No summary: the reader asked to
    // review a take, not to open a file.
    static_cast<void>(
        scoreImport.apply(loaded.scoreImport, loaded.scoreFile.getFullPathName().toStdString()));
    followNewScore(loaded.scoreImport);

    // Kept as if it had just ended here, and reviewed; it is already in the
    // library, so the saver passes it by.
    const std::uint64_t id = takeRecorder.keep(*loaded.take);
    showLibraryStatus({});
    openToolById("take-review");
    // Played as well, when that is what was asked (Play in the Library).
    if (playLoadedTake)
    {
        playLoadedTake = false;
        static_cast<void>(playTake(id));
    }
    // The review is in the main window, so that comes forward.
    if (auto* window = getTopLevelComponent())
    {
        window->toFront(true);
    }
}

void MainComponent::addSongsToLibrary()
{
    libraryChooser = std::make_unique<juce::FileChooser>(
        l10n::text("library.song.add.title"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), songPatterns, true);
    const Safe safeThis(this);
    libraryChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
            {
                return;
            }
            const juce::Array<juce::File> chosen = chooser.getResults();
            safeThis->libraryChooser.reset();
            if (chosen.isEmpty())
            {
                return;
            }

            // Each is imported first, off the message thread, and copied in
            // only if it imports, as opening one is.
            const library::Layout layout = safeThis->libraryLayout();
            safeThis->libraryWork.post(
                [chosen, layout, safeThis]() -> library::BackgroundWork::Finish
                {
                    int added = 0;
                    juce::StringArray refused;
                    for (const juce::File& file : chosen)
                    {
                        const bool imports =
                            score::musicxml::importMusicXmlFile(file).score != nullptr;
                        if (imports && library::intoLibrary(layout, file) != juce::File())
                        {
                            ++added;
                        }
                        else
                        {
                            refused.add(file.getFileName());
                        }
                    }
                    return [safeThis, added, refused]
                    {
                        if (safeThis == nullptr)
                        {
                            return;
                        }
                        safeThis->showLibraryStatus(
                            refused.isEmpty() ? l10n::plural("library.song.added", added)
                                              : l10n::format(
                                                    "library.song.not-added",
                                                    {{"files", refused.joinIntoString(", ")}}));
                        safeThis->refreshLibrary();
                    };
                });
        });
}

void MainComponent::removeLibrarySong(const library::SongEntry& song)
{
    if (song.builtIn)
    {
        return;
    }
    const juce::File file = song.file;
    const Safe safeThis(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, l10n::text("library.song.remove.title"),
        l10n::format(
            "library.song.remove.question",
            {{"title", juce::String::fromUTF8(song.title.c_str())}, {"file", file.getFileName()}}),
        l10n::text("library.song.remove.confirm"), l10n::text("library.dialog.cancel"),
        libraryWindow.get(),
        juce::ModalCallbackFunction::create(
            [safeThis, file](int response)
            {
                if (safeThis == nullptr || response == 0)
                {
                    return;
                }
                safeThis->showLibraryStatus(
                    library::moveToBin(file)
                        ? juce::String()
                        : l10n::format("library.trash-failed", {{"file", file.getFileName()}}));
                safeThis->refreshLibrary();
            }));
}

void MainComponent::deleteLibraryTake(const library::TakeEntry& entry)
{
    const juce::File folder = entry.folder;
    const juce::String what =
        entry.manifest.has_value()
            ? l10n::format(
                  "library.take.describe",
                  {{"title", juce::String::fromUTF8(entry.manifest->scoreTitle.c_str())},
                   {"when", juce::Time(entry.manifest->recordedMs).formatted("%Y-%m-%d %H:%M")}})
            : folder.getFileName();
    const Safe safeThis(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, l10n::text("library.take.delete.title"),
        l10n::format("library.take.delete.question", {{"take", what}}),
        l10n::text("library.take.delete.confirm"), l10n::text("library.dialog.cancel"),
        libraryWindow.get(),
        juce::ModalCallbackFunction::create(
            [safeThis, folder](int response)
            {
                if (safeThis == nullptr || response == 0)
                {
                    return;
                }
                safeThis->showLibraryStatus(
                    library::moveToBin(folder)
                        ? juce::String()
                        : l10n::format("library.trash-failed", {{"file", folder.getFileName()}}));
                safeThis->refreshLibrary();
            }));
}

void MainComponent::changeLibraryFolder()
{
    libraryChooser = std::make_unique<juce::FileChooser>(
        l10n::text("library.folder.choose"), libraryLayout().root, juce::String(), true);
    const Safe safeThis(this);
    libraryChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
            {
                return;
            }
            const juce::File chosen = chooser.getResult();
            safeThis->libraryChooser.reset();
            if (chosen == juce::File() || !chosen.isDirectory())
            {
                return;
            }
            // Pointing at a folder moves nothing: the old one stays as it was,
            // and pointing back finds it all again (decision 1).
            safeThis->libraryFolderSetting =
                chosen == library::defaultFolder() ? juce::String() : chosen.getFullPathName();
            safeThis->saveSettings(false);
            safeThis->showLibraryStatus({});
            safeThis->refreshLibrary();
        });
}
