#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Score.h"

// What a singer does through one part, in order: notes, with their ties folded
// in, and rests, marked with their lyrics, repeats and phrase starts (no JUCE
// dependency). openspec/changes/archive/2026-11-09-now-and-next/design.md, decision 1.
namespace score
{
enum class TimelineKind
{
    note,
    rest
};

struct TimelineItem
{
    TimelineKind kind = TimelineKind::rest;

    // Absolute, in score ticks. A note tied forward ends where its last tie
    // does.
    Tick start = 0;
    Tick end = 0;

    // The sounding pitches, lowest first; empty for a rest. A chord is one
    // item.
    std::vector<Pitch> pitches;

    // Where the attack is written, for a note.
    std::optional<NoteRef> ref;

    // Verse 1's syllable, with a hyphen after it when the word goes on.
    std::string lyric;

    // Held over at least one tie.
    bool tiedOver = false;

    // The same pitches as the note before it, even across a rest: sung again.
    bool repeated = false;

    // The first note, or a note after a rest: where a singer breathes.
    bool phraseStart = false;

    [[nodiscard]] bool isNote() const noexcept
    {
        return kind == TimelineKind::note;
    }
};

using PartTimeline = std::vector<TimelineItem>;

// The timeline of `partIndex`'s first voice; empty for a part the score lacks.
[[nodiscard]] PartTimeline timelineOf(const Score& source, std::size_t partIndex);

// The item sounding at `tick`: the last that starts at or before it and has
// not ended. Nothing before the first item or after the last.
[[nodiscard]] std::optional<std::size_t> itemAt(const PartTimeline& timeline, double tick);

// The first item that starts after `tick`.
[[nodiscard]] std::optional<std::size_t> itemAfter(const PartTimeline& timeline, double tick);
} // namespace score
