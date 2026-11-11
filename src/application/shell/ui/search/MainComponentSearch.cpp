#include "../../../localisation/Localisation.h"
#include "../../MainComponent.h"

#include "../score/BundledSampleScore.h"
#include "../settings/SettingsWindow.h"
#include "../workspace/model/WorkspaceBuiltIns.h"
#include "../workspace/model/WorkspaceMenuModel.h"
#include "SearchPopup.h"

#include "../../../search/SearchCatalogue.h"

#include <memory>
#include <utility>

// The shell's half of search (openspec/changes/archive/2026-11-10-everything-search): what is in
// the index and when it is rebuilt, the magnifying glass and Ctrl+K, and what
// choosing a result does.
//
// Every kind is rebuilt when the search opens, from the state it describes at
// that moment -- the settings in the interface language, the commands with
// their current wording, the workspaces, the open score -- except the
// library's songs and takes, which are listed on the library's worker and
// arrive a moment later, searching again for what is typed when they do.

namespace
{
using Safe = juce::Component::SafePointer<MainComponent>;

// How many results the index is asked for; the popup shows the best of them.
constexpr std::size_t searchedResults = 24;

constexpr int popupWidth = 600;
} // namespace

std::vector<MainComponent::SearchCommand> MainComponent::searchCommands()
{
    const juce::String file = l10n::text("title.file");
    const juce::String settings = l10n::text("title.settings");
    const juce::String tools = l10n::text("title.tools");
    const juce::String help = l10n::text("title.help");
    const juce::String transport = l10n::text("search.transport");
    const bool muted = audioInputService.isMuted();

    std::vector<SearchCommand> commands{
        {{"file.open-score", l10n::text("menu.file.open-score"), file, "musicxml mxl"},
         [this] { openScore(); }},
        {{"file.open-sample", bundled_sample::menuLabel(), file, "sample mahler"},
         [this] { openSampleScore(); }},
        {{"file.library", l10n::text("menu.file.library"), file,
          l10n::text("search.library.keywords")},
         [this] { showLibrary(); }},
        {{"settings.open", l10n::text("menu.settings.open"), settings, "preferences options"},
         [this] { showSettings(); }},
        {{"settings.theme.light", l10n::text("menu.settings.theme.light"), settings, "theme"},
         [this] { setTheme(Theme::light); }},
        {{"settings.theme.dark", l10n::text("menu.settings.theme.dark"), settings, "theme"},
         [this] { setTheme(Theme::dark); }},
        {{"settings.microphone",
          l10n::text(
              muted ? l10n::key("settings.audio.microphone.unmute")
                    : l10n::key("settings.audio.microphone.mute")),
          settings, "microphone"},
         [this] { audioInputService.toggleMuted(); }},
        {{"settings.demo", l10n::text("demo.menu"), settings, "demo"},
         [this] { setDemoMode(!demoMode); }},
        {{"settings.import", l10n::text("settings.import"), settings, "ptsettings"},
         [this] { importSettings(); }},
        {{"settings.export", l10n::text("settings.export"), settings, "ptsettings"},
         [this] { exportSettings(); }},
        {{"tools.side-by-side", l10n::text("menu.arrange.side-by-side"), tools,
          l10n::text("menu.arrange")},
         [this] { setWorkspaceLayout(WorkspaceLayoutState::Layout::horizontal); }},
        {{"tools.stacked", l10n::text("menu.arrange.stacked"), tools, l10n::text("menu.arrange")},
         [this] { setWorkspaceLayout(WorkspaceLayoutState::Layout::vertical); }},
        {{"tools.tabbed", l10n::text("menu.arrange.tabbed"), tools, l10n::text("menu.arrange")},
         [this] { setWorkspaceLayout(WorkspaceLayoutState::Layout::tabbed); }},
        {{"tools.save-workspace", l10n::text("menu.workspaces.save-as"), tools,
          l10n::text("menu.workspaces")},
         [this] { showSaveWorkspaceDialog(); }},
        {{"tools.clear-target", l10n::text("target.clear"), tools, "target note"},
         [this] { clearTarget(); }},
        {{"help.feedback", l10n::text("menu.help.feedback"), help, "bug report"},
         [this] { showFeedback(); }},
    };
    // The transport's, only with a score to play.
    if (scoreImport.hasScore())
    {
        commands.push_back(
            {{"transport.play", l10n::text("transport.play"), transport,
              l10n::text("transport.pause")},
             [this] { practiceClock.togglePlay(); }});
        commands.push_back(
            {{"transport.stop", l10n::text("transport.stop"), transport, {}},
             [this] { practiceClock.stop(); }});
        commands.push_back(
            {{"transport.record", l10n::text("transport.record"), transport, "take"},
             [this] { static_cast<void>(takeRecorder.start()); }});
    }
    return commands;
}

void MainComponent::refreshSearchIndex()
{
    if (!searchIndex.usable())
    {
        return;
    }
    static_cast<void>(searchIndex.replace(search::kind::setting, search::settingsDocuments()));
    static_cast<void>(searchIndex.replace(search::kind::tool, search::toolDocuments()));

    currentSearchCommands = searchCommands();
    std::vector<search::Command> commands;
    commands.reserve(currentSearchCommands.size());
    for (const auto& command : currentSearchCommands)
    {
        commands.push_back(command.command);
    }
    static_cast<void>(
        searchIndex.replace(search::kind::command, search::commandDocuments(commands)));

    std::vector<search::SearchDocument> workspaces;
    for (const auto& entry : WorkspaceMenuModel::fromCatalog(workspaceCatalog).entries)
    {
        workspaces.push_back(search::SearchDocument{
            entry.id,
            (entry.builtIn ? l10n::text(WorkspaceBuiltIns::nameKey(entry.id))
                           : juce::String::fromUTF8(entry.name.c_str()))
                .toStdString(),
            l10n::text("menu.workspaces").toStdString(),
            "layout",
            {}});
    }
    static_cast<void>(searchIndex.replace(search::kind::workspace, workspaces));

    // The open score's lines and bars, only when the score changed: a long
    // score has many of each.
    const std::shared_ptr<const score::Score> open = scoreImport.score();
    if (open != searchIndexedScore)
    {
        searchIndexedScore = open;
        static_cast<void>(searchIndex.replace(
            search::kind::lyric, open != nullptr ? search::lyricDocuments(*open)
                                                 : std::vector<search::SearchDocument>{}));
        static_cast<void>(searchIndex.replace(
            search::kind::bar,
            open != nullptr ? search::barDocuments(*open) : std::vector<search::SearchDocument>{}));
    }

    // The library, from its worker; the results come back to search again.
    if (!libraryReachable())
    {
        return;
    }
    const library::Layout layout = libraryLayout();
    const juce::File samples = bundled_sample::defaultFolder();
    const Safe safeThis(this);
    libraryWork.post(
        [layout, samples, safeThis]() -> library::BackgroundWork::Finish
        {
            auto songs = std::make_shared<std::vector<search::SearchDocument>>(
                search::songDocuments(library::listSongs(layout, samples)));
            auto takes = std::make_shared<std::vector<search::SearchDocument>>(
                search::takeDocuments(library::listTakes(layout)));
            return [safeThis, songs, takes]
            {
                if (safeThis == nullptr)
                {
                    return;
                }
                static_cast<void>(safeThis->searchIndex.replace(search::kind::song, *songs));
                static_cast<void>(safeThis->searchIndex.replace(search::kind::take, *takes));
                if (safeThis->searchPopup != nullptr)
                {
                    safeThis->searchPopup->refresh();
                }
            };
        });
}

void MainComponent::showSearch()
{
    if (searchPopup != nullptr)
    {
        searchPopup->focus();
        return;
    }
    refreshSearchIndex();

    const Safe safeThis(this);
    searchPopup = std::make_unique<SearchPopup>(
        [safeThis](const juce::String& query)
        {
            return safeThis != nullptr
                       ? safeThis->searchIndex.search(query.toStdString(), searchedResults)
                       : std::vector<search::SearchHit>{};
        },
        [safeThis](const search::SearchHit& hit)
        {
            if (safeThis != nullptr)
            {
                safeThis->chooseSearchHit(hit);
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->closeSearch();
            }
        });
    addAndMakeVisible(*searchPopup);
    placeSearch();
    searchPopup->toFront(false);
    searchPopup->focus();
}

void MainComponent::closeSearch()
{
    searchPopup.reset();
}

void MainComponent::placeSearch()
{
    if (searchPopup == nullptr)
    {
        return;
    }
    // Under the magnifying glass, at the top right, as wide as fits.
    const int margin = 12;
    const int width = std::min(appearance::scaledFor(*this, popupWidth), getWidth() - 2 * margin);
    const int height = std::min(searchPopup->preferredHeight(), getHeight() - 2 * margin);
    searchPopup->setBounds(getWidth() - margin - width, 6, width, height);
    // Above the workspace, which a rebuild adds after it.
    searchPopup->toFront(false);
}

void MainComponent::chooseSearchHit(const search::SearchHit& hit)
{
    const std::string_view kind = hit.kind;
    if (kind == search::kind::setting)
    {
        showSettings();
        const auto* panel = search::panelOf(hit.id);
        if (settingsWindow != nullptr && panel != nullptr)
        {
            const juce::String identifier(std::string(panel->identifier));
            // A panel's own entry opens it; a control's rings it there.
            if (hit.id == std::string(panel->prefix) + "title")
            {
                static_cast<void>(settingsWindow->showPanel(identifier));
            }
            else
            {
                static_cast<void>(settingsWindow->reveal(identifier, l10n::text(hit.id)));
            }
        }
    }
    else if (kind == search::kind::tool)
    {
        openToolById(hit.id);
    }
    else if (kind == search::kind::command)
    {
        for (const auto& command : currentSearchCommands)
        {
            if (command.command.id == hit.id)
            {
                // Copied: a command can rebuild the list it is in.
                const auto run = command.run;
                run();
                break;
            }
        }
    }
    else if (kind == search::kind::workspace)
    {
        applyWorkspace(hit.id);
    }
    else if (kind == search::kind::song)
    {
        startScoreImport(juce::File(juce::String::fromUTF8(hit.id.c_str())));
    }
    else if (kind == search::kind::take)
    {
        reviewLibraryTake(juce::File(juce::String::fromUTF8(hit.id.c_str())));
    }
    else if (kind == search::kind::lyric || kind == search::kind::bar)
    {
        // The id is the tick to move to; the Score View shows where.
        practiceClock.seek(static_cast<score::Tick>(std::stoll(hit.id)));
        openToolById("score-view");
    }
}

bool MainComponent::SearchShortcut::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Ctrl+K, and Ctrl+F, the two a reader is likely to try; Cmd on a Mac.
    const auto withCommand = [&key](int letter)
    {
        return key == juce::KeyPress(letter, juce::ModifierKeys::commandModifier, 0) ||
               key == juce::KeyPress(letter - 'a' + 'A', juce::ModifierKeys::commandModifier, 0);
    };
    if (withCommand('k') || withCommand('f'))
    {
        owner.showSearch();
        return true;
    }
    return false;
}

void MainComponent::parentHierarchyChanged()
{
    // On the window itself, so the shortcut works wherever the focus is -- and
    // when nothing has it, which is when keys go to the window.
    auto* window = getTopLevelComponent();
    if (window == shortcutWindow)
    {
        return;
    }
    if (shortcutWindow != nullptr)
    {
        shortcutWindow->removeKeyListener(&searchShortcut);
    }
    shortcutWindow = window != this ? window : nullptr;
    if (shortcutWindow != nullptr)
    {
        shortcutWindow->addKeyListener(&searchShortcut);
    }
}
