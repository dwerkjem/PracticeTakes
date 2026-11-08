#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../take/Take.h"
#include "OnsetTiming.h"

// Which written notes of a take were right, wrong, missed or not judged, and
// which played notes were extra (no JUCE dependency).
// openspec/changes/archive/2026-11-08-note-matching/design.md.
namespace evaluation
{
enum class NoteOutcome
{
    correct,
    wrong,
    missed,

    // Played at the right moment, but the evidence has no pitch: a microphone
    // onset before #42 gives it one.
    pitchNotJudged
};

struct NoteResult
{
    std::size_t part = 0;
    score::NoteRef ref;
    score::Tick tick = 0;
    int writtenMidi = 60;

    NoteOutcome outcome = NoteOutcome::missed;

    // What was played instead, for a wrong note.
    std::optional<int> playedMidi;

    // The pairing that made this a wrong note had an equally good alternative.
    bool ambiguous = false;

    // The moment's timing (#43), shared by every note of it.
    TimingVerdict timing = TimingVerdict::absent;
    double timingErrorMs = 0.0;
};

struct ExtraNote
{
    std::int64_t frame = 0;

    // Unknown for a microphone onset until #42.
    std::optional<int> midi;
};

struct NoteSummary
{
    std::size_t correct = 0;
    std::size_t wrong = 0;
    std::size_t missed = 0;
    std::size_t pitchNotJudged = 0;
    std::size_t extra = 0;
};

struct NoteReport
{
    std::vector<NoteResult> results;
    std::vector<ExtraNote> extra;
    NoteSummary summary;
};

// Compare pitches within the moments `timing` matched (decision 1).
[[nodiscard]] NoteReport matchNotes(const TimingReport& timing, std::size_t part);

// Timing, then matching, for one part of a take's score.
[[nodiscard]] NoteReport
judgeNotes(const take::Take& take, std::size_t part, const TimingSettings& settings = {});
} // namespace evaluation
