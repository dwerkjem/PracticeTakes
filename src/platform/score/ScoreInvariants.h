#pragma once

#include "Score.h"

// The ten invariants the score model guarantees to its consumers.
//
// These are the reason the model is worth having as a thing separate from the
// parser: a renderer, a playback engine, and a session file can each rely on
// them without re-deriving them, and a test can check them without a parser.
//
// Every violation is a **repair plus a diagnostic, never a throw**. A score
// arrives from a stranger's file, so "this file is wrong" has to produce
// something a musician can still practise with, not an exception. The trade is
// explicit: the resulting score is complete and self-consistent, but no longer
// a faithful transcription of the file, and the diagnostic is what says so.
//
// Invariant 10 (no engraving data) has no runtime check. It is enforced by the
// types: there is nowhere in Score, Part, Measure, Voice, or ScoreEvent to put
// a coordinate, a font, a page break, a stem direction, or a beam group. That
// is deliberate -- a check can be forgotten, a missing field cannot be filled.
namespace score
{
// Apply every invariant to `score` in place, appending a diagnostic for each
// repair made.
//
// Order matters and is not arbitrary. Pitches and durations are normalised
// first because later steps sort and bound by duration; voice ordering and
// measure bounds come next because they can drop events; cross-part alignment
// then fixes up indices and start ticks; ties are validated **last**, once no
// step can still move or remove a note out from under a NoteRef.
void enforceInvariants(Score& score);

// The individual invariants, exposed so each can be tested on its own with a
// score that satisfies it and a score that must be repaired.
//
// Each appends to `score.diagnostics` and returns the number of repairs made,
// so a test can assert "this was already valid" as well as "this was fixed".

// Invariant 5: every note's sounding number matches its spelling. Repaired by
// recomputing the number from the spelling, because the spelling is what the
// engraver wrote and the number is derived data.
int enforceConsistentPitches(Score& score);

// Invariant 3: durations are non-negative, and only grace notes are zero.
// Negative durations clamp to zero; a zero-duration event that is not a grace
// note occupies no time and cannot be rendered or played, so it is dropped.
int enforceNonNegativeDurations(Score& score);

// Invariant 2: a voice's events are in ascending onset order and do not
// overlap. Repaired by stable-sorting on onset, then truncating any event that
// runs into the next one. A chord is one event, so it never overlaps itself.
int enforceVoiceOrdering(Score& score);

// Invariant 7: a voice does not extend past its measure's nominal duration,
// except in a pickup measure, where it may be shorter. Over-full measures are
// truncated -- events starting past the end are dropped, and one straddling it
// is shortened.
int enforceMeasureBounds(Score& score);

// Invariant 1: every part has the same number of measures, and measure n has
// the same start and nominal duration in every part. Short parts are padded
// with whole-measure rests; disagreeing nominal durations take the longest
// claim; starts are then recomputed cumulatively so they cannot drift.
int enforceMeasureAlignment(Score& score);

// Invariant 6: part identifiers are unique and non-empty. Missing or duplicate
// ids get generated ones -- #39's session file needs to name a part stably, and
// two parts sharing an id makes that impossible.
int enforceUniquePartIds(Score& score);

// Invariant 4: a tie links two **notes** that both exist and sound the same
// pitch, and the link is mirrored at both ends. Anything else is dropped
// rather than left dangling, because a half-linked tie is a null dereference
// waiting to happen in every consumer.
//
// Notes rather than events, because a chord may tie some of its notes and not
// others -- a pianist holding a bass note while the upper voices move. An
// event-level tie could not express that.
int enforceTieIntegrity(Score& score);

// Invariant 8: the tempo map is non-empty, sorted, and free of duplicate
// ticks. Delegates to TempoMap::build, which is where those rules live.
int enforceTempoMap(Score& score);

// Invariant 9: a diagnostic never names a part, measure, or voice that is not
// in the score. Offending fields are cleared rather than the diagnostic being
// dropped, because the message is still worth showing -- it just cannot be
// pinned to a location. Adds no diagnostic of its own; doing so would be a
// diagnostic about diagnostics.
int enforceDiagnosticLocations(Score& score);
} // namespace score
