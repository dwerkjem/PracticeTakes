#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "platform/score/Measure.h"
#include "platform/score/MusicalTime.h"
#include "platform/score/Score.h"
#include "platform/score/ScoreEvent.h"
#include "platform/score/TempoMap.h"
#include "platform/score/musicxml/MusicXmlDiagnosticSink.h"

// The state the readers share while a document is being turned into a score.
//
// Passed by reference rather than held by one big reader class, so that reading
// a measure, a part, and a document can each live in their own file and be
// understood on their own. Everything mutable during an import is here, and
// nothing here outlives the import.
namespace score::musicxml
{
// The tie markings MusicXML wrote on one note. A note may carry both, which is
// what a note in the middle of a chain of ties looks like.
struct PendingTie
{
    bool start = false;
    bool stop = false;
};

// An event plus the tie flags the file wrote on each of its notes.
//
// The flags live beside the event rather than on it because a tie resolves to a
// NoteRef, and a NoteRef is only knowable once a voice's events are in their
// final order -- which is after the measure has been read.
//
// One entry per note, kept in step with `event.notes`. They are appended
// together and never separately, which is what keeps the two aligned.
struct PendingEvent
{
    ScoreEvent event;
    std::vector<PendingTie> noteTies;
};

struct PendingVoice
{
    int number = 1;
    std::vector<PendingEvent> events;
};

// What carries from one measure to the next within a part.
struct PartCursorState
{
    // Source <divisions> per quarter note, as most recently declared. MusicXML
    // declares this per part and permits redeclaring it in any measure, which
    // is why it is state rather than a constant.
    Tick divisions = 1;
    bool divisionsDeclared = false;

    TimeSignature time;
    int staffCount = 1;

    // Transposition in force per staff, by staff number. Absent means the staff
    // sounds as written, which is every staff of most scores.
    std::map<int, Transposition> transpositions;
};

struct ReadContext
{
    MusicXmlDiagnosticSink diagnostics;

    // Tempo changes gathered from every part, to become the score's TempoMap.
    // Collected across parts because tempo is a property of the score, not of
    // whichever part happened to carry the marking.
    std::vector<TempoEntry> tempoEntries;

    // Notes, chords, and rests read so far. Task 7.5 turns zero of them into a
    // structural failure, so it has to be counted as the document is read.
    std::size_t eventCount = 0;

    // Serial for the identifiers generated when a file's own are missing or
    // duplicated (invariant 6).
    int generatedPartIds = 0;
};

// Diagnostic locations, built from whatever is in scope. Free functions rather
// than members so a reader that only has a measure can still name one.
[[nodiscard]] DiagnosticLocation locationOf(const Part& part);
[[nodiscard]] DiagnosticLocation locationOf(const Part& part, const Measure& measure);
[[nodiscard]] DiagnosticLocation locationOf(const Part& part, const Measure& measure, int voice);
} // namespace score::musicxml
