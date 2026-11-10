# search Specification

## Purpose
TBD - created by archiving change everything-search. Update Purpose after archive.

## Requirements

### Requirement: One search over the application
A magnifying glass in the title strip, and Ctrl+K (Cmd+K) or Ctrl+F from
anywhere, SHALL open a search bar. The search bar SHALL search, as the reader
types, the Settings controls and panels, the tools, the menus' commands, the
workspaces, the library's songs and takes, and the open score's lyric lines
and bars. It SHALL list the best matches, each with its kind and where it is.

#### Scenario: Find a setting
- **WHEN** the reader types "pitch"
- **THEN** the Pitch and Reference pitch settings are listed first, marked as
  settings in Practice

#### Scenario: Find a line of the lyrics
- **WHEN** the sample score is open and the reader types "augen"
- **THEN** the lines that sing it are listed with their bar and part

### Requirement: Typing is forgiving
Every typed word SHALL have to match. The last word MAY be half-typed, a word of
four or more letters MAY be one letter out, and case and accents SHALL NOT
matter. No text the reader types SHALL make a search fail.

#### Scenario: A typo
- **WHEN** the reader types "refrence"
- **THEN** the Reference pitch setting is found

#### Scenario: Characters with meaning elsewhere
- **WHEN** the reader types `C# "unclosed (`
- **THEN** the search returns results or none, and does not fail

### Requirement: Choosing a result acts on it
Choosing a result SHALL:
- open Settings on a setting's panel with the control shown;
- open a tool;
- run a command;
- open a song;
- review a take;
- apply a workspace;
- move the practice clock to a lyric line's or a bar's place.

Escape or a click outside SHALL close the search bar without acting.

#### Scenario: Go to a lyric
- **WHEN** the reader chooses the line "blauen Augen"
- **THEN** the score cursor is at the start of that line, in bar 2

### Requirement: The index follows the application
What the search finds SHALL be the application's state when the search is
opened: the interface language's words, the commands' current wording, the open
score, and the library's songs and takes as they are on disk.

#### Scenario: A new take
- **WHEN** a take is recorded and the search is opened
- **THEN** the take can be found by its song's name
