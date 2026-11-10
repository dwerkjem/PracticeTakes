#include "SearchCatalogue.h"

#include "../localisation/Localisation.h"
#include "../tools/BuiltInToolCatalog.h"

#include "platform/search/ScoreSearch.h"

#include <algorithm>
#include <array>
#include <set>

namespace
{
std::string utf8(const juce::String& text)
{
    return text.toStdString();
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// A sentence with a {placeholder} is a message about a value, not the name of
// a control, and reads as nonsense with the brace in it.
bool isPlainText(const juce::String& text)
{
    return !text.containsChar('{');
}

// Longer than any control's label: a sentence of explanation, which the panel
// shows as prose and which is no name to search a control by.
constexpr int maximumLabelLength = 60;

juce::String endingOf(const library::TakeManifest& manifest)
{
    if (manifest.state == take::TakeState::completed)
    {
        return l10n::text("library.ended.completed");
    }
    if (manifest.state == take::TakeState::failed)
    {
        return l10n::text("library.ended.failed");
    }
    return manifest.stoppedInBar.has_value()
               ? l10n::format(
                     "library.ended.stopped-in-bar",
                     {{"bar", l10n::number(static_cast<double>(*manifest.stoppedInBar + 1), 0)}})
               : l10n::text("library.ended.stopped");
}

juce::String inputsOf(const library::TakeManifest& manifest)
{
    juce::StringArray words;
    if (manifest.usedMicrophone)
    {
        words.add(l10n::text("library.inputs.microphone"));
    }
    if (manifest.usedKeyboard)
    {
        words.add(l10n::text("library.inputs.keyboard"));
    }
    return words.joinIntoString(" ");
}
} // namespace

namespace search
{
const std::vector<SettingsPanel>& settingsPanels()
{
    static const std::vector<SettingsPanel> panels{
        {"settings.appearance.", "appearance"},
        {"settings.audio.", "audio-device"},
        {"settings.midi.", "midi"},
        {"settings.practice.", "practice"},
        {"settings.reset.", "reset"},
    };
    return panels;
}

const SettingsPanel* panelOf(std::string_view key)
{
    for (const SettingsPanel& panel : settingsPanels())
    {
        if (startsWith(key, panel.prefix))
        {
            return &panel;
        }
    }
    return nullptr;
}

std::vector<SearchDocument> settingsDocuments()
{
    const std::vector<std::string> keys = l10n::current().keysStartingWith("settings.");
    const std::set<std::string_view> known(keys.begin(), keys.end());

    std::vector<SearchDocument> documents;
    for (const SettingsPanel& panel : settingsPanels())
    {
        const std::string title = std::string(panel.prefix) + "title";
        const std::string description = std::string(panel.prefix) + "description";
        if (known.count(title) != 0)
        {
            documents.push_back(SearchDocument{
                title,
                utf8(l10n::text(title)),
                utf8(l10n::text("search.settings")),
                {},
                known.count(description) != 0 ? utf8(l10n::text(description)) : std::string()});
        }
    }

    // What a control's own sentence is called, beside its label: shown as a
    // tooltip, read out as its title, or asked before it acts.
    constexpr std::array<std::string_view, 3> explanations{".tooltip", ".title", ".question"};
    // Keys beneath a control that are not one of its choices.
    constexpr std::array<std::string_view, 4> notChoices{
        ".tooltip", ".title", ".question", ".prompt"};

    for (const std::string& key : keys)
    {
        const SettingsPanel* panel = panelOf(key);
        if (panel == nullptr)
        {
            continue;
        }
        // Directly under the panel: settings.<panel>.<name>.
        const std::string_view name = std::string_view(key).substr(panel->prefix.size());
        if (name.find('.') != std::string_view::npos || name == "title" || name == "description" ||
            name == "heading")
        {
            continue;
        }
        const juce::String label = l10n::text(key);
        // A label, not prose and not a message about a value.
        if (!isPlainText(label) || label.length() > maximumLabelLength)
        {
            continue;
        }

        std::string body;
        for (const std::string_view suffix : explanations)
        {
            if (const std::string explained = key + std::string(suffix);
                known.count(explained) != 0)
            {
                body = utf8(l10n::text(explained));
                break;
            }
        }

        // Its choices' words: "Sharps", "Flats", "None".
        juce::StringArray choices;
        const std::string below = key + ".";
        for (auto other = known.lower_bound(below);
             other != known.end() && startsWith(*other, below); ++other)
        {
            const bool explanation = std::any_of(
                notChoices.begin(), notChoices.end(),
                [&other](std::string_view suffix) { return endsWith(*other, suffix); });
            const juce::String choice = l10n::text(*other);
            if (!explanation && isPlainText(choice) && choice.length() <= maximumLabelLength)
            {
                choices.add(choice);
            }
        }
        documents.push_back(SearchDocument{
            key, utf8(label.trimCharactersAtEnd(":")),
            utf8(l10n::text(std::string(panel->prefix) + "title")),
            utf8(choices.joinIntoString(" ")), body});
    }
    return documents;
}

std::vector<SearchDocument> toolDocuments()
{
    std::vector<SearchDocument> documents;
    for (const ToolDefinition& tool : builtInToolCatalog().tools())
    {
        juce::StringArray keywords;
        keywords.add(juce::String(tool.displayName));
        for (const std::string& alias : tool.aliases)
        {
            keywords.add(juce::String(alias));
        }
        documents.push_back(SearchDocument{
            tool.id,
            utf8(l10n::text(toolNameKey(tool.id))),
            utf8(l10n::text("search.tools")),
            utf8(keywords.joinIntoString(" ")),
            {}});
    }
    return documents;
}

std::vector<SearchDocument> commandDocuments(const std::vector<Command>& commands)
{
    std::vector<SearchDocument> documents;
    documents.reserve(commands.size());
    for (const Command& command : commands)
    {
        documents.push_back(SearchDocument{
            command.id,
            utf8(command.title.trimCharactersAtEnd(".…")),
            utf8(command.menu),
            utf8(command.keywords),
            {}});
    }
    return documents;
}

std::vector<SearchDocument> songDocuments(const std::vector<library::SongEntry>& songs)
{
    std::vector<SearchDocument> documents;
    for (const library::SongEntry& song : songs)
    {
        if (!song.readable)
        {
            continue;
        }
        std::string parts;
        for (const std::string& part : song.parts)
        {
            parts += (parts.empty() ? "" : ", ") + part;
        }
        documents.push_back(SearchDocument{
            song.file.getFullPathName().toStdString(), song.title, song.composer,
            song.file.getFileNameWithoutExtension().toStdString(), parts});
    }
    return documents;
}

std::vector<SearchDocument> takeDocuments(const std::vector<library::TakeEntry>& takes)
{
    std::vector<SearchDocument> documents;
    for (const library::TakeEntry& entry : takes)
    {
        if (!entry.manifest.has_value())
        {
            continue;
        }
        const library::TakeManifest& manifest = *entry.manifest;
        const juce::String when = juce::Time(manifest.recordedMs).formatted("%Y-%m-%d %H:%M");
        documents.push_back(SearchDocument{
            entry.folder.getFullPathName().toStdString(),
            utf8(l10n::format(
                "search.take.title",
                {{"title", juce::String::fromUTF8(manifest.scoreTitle.c_str())}})),
            utf8(when + " · " + endingOf(manifest)),
            utf8(inputsOf(manifest) + " " + juce::Time(manifest.recordedMs).formatted("%B %A")),
            {}});
    }
    return documents;
}

std::vector<SearchDocument> lyricDocuments(const score::Score& source)
{
    std::vector<SearchDocument> documents;
    for (const LyricPhrase& phrase : lyricPhrasesOf(source))
    {
        documents.push_back(SearchDocument{
            std::to_string(phrase.tick),
            phrase.text,
            utf8(l10n::format(
                "search.lyric.detail",
                {{"bar", juce::String::fromUTF8(phrase.bar.c_str())},
                 {"part", juce::String::fromUTF8(phrase.partName.c_str())}})),
            {},
            {}});
    }
    return documents;
}

std::vector<SearchDocument> barDocuments(const score::Score& source)
{
    std::vector<SearchDocument> documents;
    const std::string keywords = utf8(l10n::text("search.bar.keywords"));
    for (const ScoreBar& bar : barsOf(source))
    {
        documents.push_back(SearchDocument{
            std::to_string(bar.tick),
            utf8(l10n::format(
                "search.bar.title", {{"bar", juce::String::fromUTF8(bar.bar.c_str())}})),
            {},
            keywords,
            {}});
    }
    return documents;
}

juce::String kindLabel(std::string_view resultKind)
{
    if (resultKind == kind::setting)
    {
        return l10n::text("search.kind.setting");
    }
    if (resultKind == kind::tool)
    {
        return l10n::text("search.kind.tool");
    }
    if (resultKind == kind::command)
    {
        return l10n::text("search.kind.command");
    }
    if (resultKind == kind::workspace)
    {
        return l10n::text("search.kind.workspace");
    }
    if (resultKind == kind::song)
    {
        return l10n::text("search.kind.song");
    }
    if (resultKind == kind::take)
    {
        return l10n::text("search.kind.take");
    }
    if (resultKind == kind::lyric)
    {
        return l10n::text("search.kind.lyric");
    }
    return resultKind == kind::bar ? l10n::text("search.kind.bar") : juce::String();
}
} // namespace search
