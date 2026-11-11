#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../take/Take.h"
#include "Intonation.h"
#include "OnsetTiming.h"
#include "PitchContour.h"

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

    // Played at the right moment, but its pitch is not judged: a chord from
    // the microphone, or a sung note whose pitch was uncertain (`intonation`
    // says which).
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
    std::optional<pitch::IntonationZone> timingZone;

    // How in tune a single sung note was (#42): set for every single note of a
    // microphone take that timing matched.
    std::optional<Intonation> intonation;
};

struct ExtraNote
{
    std::int64_t frame = 0;

    // For a microphone onset, the note sung just after it, when the contour
    // was sure of one.
    std::optional<int> midi;
};

struct NoteSummary
{
    std::size_t correct = 0;
    std::size_t wrong = 0;
    std::size_t missed = 0;
    std::size_t pitchNotJudged = 0;
    std::size_t extra = 0;

    // Of the sung notes judged for intonation: the correct ones in tune,
    // sharp and flat, and the not-judged ones uncertain.
    std::size_t inTune = 0;
    std::size_t sharp = 0;
    std::size_t flat = 0;
    std::size_t uncertain = 0;
};

struct NoteReport
{
    std::vector<NoteResult> results;
    std::vector<ExtraNote> extra;
    NoteSummary summary;
};

// Compare pitches within the moments `timing` matched (decision 1).
[[nodiscard]] NoteReport matchNotes(const TimingReport& timing, std::size_t part);

// What a take is judged from, found once and shared by every part tried: its
// onsets, and for a microphone take the pitch contour of its audio.
struct TakeEvidence
{
    std::vector<PerformedOnset> onsets;
    PitchContour contour;
};

// The contour of a take judged from its microphone; empty otherwise.
[[nodiscard]] PitchContour contourOf(const take::Take& take);

[[nodiscard]] TakeEvidence evidenceOf(const take::Take& take, const OnsetSettings& onsets = {});

// Judge each single sung note of `report` for intonation on `contour`, and
// give an extra note the pitch sung after it
// (openspec/changes/archive/2026-11-09-sung-pitch-judging/design.md, decisions 2-6). `timing` is
// the report `report` was matched from.
void judgeIntonations(
    NoteReport& report,
    const TimingReport& timing,
    const take::Take& take,
    std::size_t part,
    const PitchContour& contour,
    const IntonationSettings& settings = {});

// Count a report's results again, after they change.
void summarise(NoteReport& report);

// Timing, then matching, for one part of a take's score, from `onsets`
// (`onsetsOf` the take). No intonation: that needs the contour.
[[nodiscard]] NoteReport judgeNotes(
    const take::Take& take,
    std::size_t part,
    const std::vector<PerformedOnset>& onsets,
    const TimingSettings& settings = {});

// Timing, matching and intonation from `evidence` (`evidenceOf` the take).
[[nodiscard]] NoteReport judgeNotes(
    const take::Take& take,
    std::size_t part,
    const TakeEvidence& evidence,
    const TimingSettings& timing = {},
    const IntonationSettings& intonation = {});

// The same, finding the evidence.
[[nodiscard]] NoteReport judgeNotes(
    const take::Take& take,
    std::size_t part,
    const TimingSettings& timing = {},
    const IntonationSettings& intonation = {});
} // namespace evaluation
