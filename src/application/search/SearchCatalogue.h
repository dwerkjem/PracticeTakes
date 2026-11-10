#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <string_view>
#include <vector>

#include "platform/library/LibraryListing.h"
#include "platform/score/Score.h"
#include "platform/search/SearchIndex.h"

// What the search bar finds, as documents for the index
// (openspec/changes/archive/2026-11-10-everything-search, decision 4). Each function turns one
// source -- the settings, the tools, the library, the open score -- into the
// documents of one kind, in the interface language. The shell decides when
// each is rebuilt and what choosing a result does.
namespace search
{
// The kinds, which the shell dispatches a chosen result on.
namespace kind
{
inline constexpr std::string_view setting = "setting";
inline constexpr std::string_view tool = "tool";
inline constexpr std::string_view command = "command";
inline constexpr std::string_view workspace = "workspace";
inline constexpr std::string_view song = "song";
inline constexpr std::string_view take = "take";
inline constexpr std::string_view lyric = "lyric";
inline constexpr std::string_view bar = "bar";
} // namespace kind

// A Settings panel, by the prefix of its catalogue keys and the identifier
// SettingsWindow::showPanel takes.
struct SettingsPanel
{
    std::string_view prefix;
    std::string_view identifier;
};

// The panels whose controls are searched.
[[nodiscard]] const std::vector<SettingsPanel>& settingsPanels();

// The panel a setting's key belongs to; null for a key that is not a setting.
[[nodiscard]] const SettingsPanel* panelOf(std::string_view key);

// Every control in Settings, from the catalogue. A key directly under a panel,
// `settings.<panel>.<name>`, is a control's label -- but for the panel's own
// title, description and heading, prose longer than a label, and messages
// with a value in them. Its panel is the detail; its tooltip, title or
// question is the body; and the words of its choices
// (`settings.<panel>.<name>.<choice>`) are its keywords. Each panel is a
// document too, by its title and description, with its title's key as the id.
[[nodiscard]] std::vector<SearchDocument> settingsDocuments();

// Every tool, by its name and the other names it answers to.
[[nodiscard]] std::vector<SearchDocument> toolDocuments();

// A command a menu offers, to be searched and run.
struct Command
{
    std::string id;
    // The words, already in the interface language.
    juce::String title;
    juce::String menu;
    juce::String keywords;
};

[[nodiscard]] std::vector<SearchDocument> commandDocuments(const std::vector<Command>& commands);

// The library's songs (by title, composer and parts; the file is the id) and
// takes (by song, date and how they ended; the folder is the id). An
// unreadable song or take is not offered: there is nothing to do with it from
// a search.
[[nodiscard]] std::vector<SearchDocument>
songDocuments(const std::vector<library::SongEntry>& songs);
[[nodiscard]] std::vector<SearchDocument>
takeDocuments(const std::vector<library::TakeEntry>& takes);

// The open score's sung phrases and its bars, each with the tick to move the
// clock to as its id.
[[nodiscard]] std::vector<SearchDocument> lyricDocuments(const score::Score& source);
[[nodiscard]] std::vector<SearchDocument> barDocuments(const score::Score& source);

// How a result is introduced in the list: "Setting", "Song", ...
[[nodiscard]] juce::String kindLabel(std::string_view resultKind);
} // namespace search
