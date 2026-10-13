#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "platform/score/Score.h"
#include "platform/score/ScoreEvent.h"

// Choosing and describing the one note the Target Note tool shows (no JUCE
// dependency).
//
// Split out of the `Component` following the `WorkspaceLayoutState` and
// `ScoreImportSummary` precedent: this is the only part of the tool a unit test
// can reach without a display, so it is where every rule worth asserting lives
// -- which event is chosen, what is skipped, which of a note's two pitches is
// the target, and how a tick position becomes a time.
//
// Deliberately in the global namespace, matching the rest of the application,
// so that `score::` keeps meaning the score model and nothing else.

// The three things that can be true, as three cases rather than a bool and an
// out-parameter: "no score open" and "this score has no note in it" are
// different sentences to put in front of a user, and collapsing them would make
// the tool say the wrong one.
enum class TargetNoteOutcome
{
    noScoreOpen,
    noPitchedNote,
    found
};

// One target note, described well enough to be found in the file and played.
struct TargetNote
{
    // Where this note is in the first part. Carried because #33 will need to
    // move from here, and because it lets a test verify the choice against the
    // score rather than against the chooser.
    score::NoteRef location;

    std::string partId;
    std::string partName;

    int staff = 1;

    // MusicXML's voice number, not a position in a list: a part may use voices
    // 1 and 5 with nothing between, and the piano fixture in
    // src/tests/resources/harness-scores does exactly that.
    int voiceNumber = 1;

    // The measure number **as printed in the file**, which is why it is a
    // string: a pickup is "0", a split bar is "12a", and some exporters leave
    // it blank. The model keeps it for display, so this is the display.
    std::string printedBarNumber;

    // The sounding pitch -- the target. `Note` states which of its two pitches
    // is for what: `sounding` is what "playback and any pitch-matching against
    // live audio must use", and a target note exists to be matched.
    std::string soundingName;
    int soundingMidiNoteNumber = 60;

    // The written spelling, and whether it differs. It differs only for a
    // transposing part, and when it does, both are worth showing: a target that
    // silently showed the written pitch of a clarinet part would be
    // consistently a tone wrong in a way nothing crashes on.
    std::string writtenName;
    bool soundsAsWritten = true;

    // Whether the event this note belongs to is a chord, and how many notes it
    // holds. A chord is one event carrying several pitches, so the target is one
    // of them, and saying so is more honest than presenting it as a lone note.
    bool isChordMember = false;
    std::size_t chordNoteCount = 1;

    // Through the score's own TempoMap, which is the one pair of numbers that
    // proves the tempo map was read rather than defaulted. The duration is the
    // difference between the note's end and its start in seconds, not the tick
    // duration converted at the starting tempo -- those differ when the tempo
    // changes while the note is sounding.
    double startSeconds = 0.0;
    double durationSeconds = 0.0;
};

struct TargetNoteSelection
{
    TargetNoteOutcome outcome = TargetNoteOutcome::noScoreOpen;

    // How to name the open score. Empty only when no score is open.
    std::string scoreTitle;

    // Present exactly when `outcome == found`.
    std::optional<TargetNote> note;
};

// The target is the **first pitched, non-grace note of the first part**: the
// earliest onset in the earliest measure that has one, tie-broken by the lower
// voice number.
//
// Deliberately dumb and deliberately not configurable. Choosing a target
// properly is #33's question and needs a cursor, a selected part, and a
// transport, none of which exist; a rule that fits in one sentence is a rule a
// test can pin, and it is enough to prove the model's accessors work.
//
// Rests are never targets, and a grace note is skipped: it is a zero-duration
// event taking its time from performance rather than from the bar's arithmetic
// (ScoreEvent.h), so it is not a useful first thing to put in front of someone.
[[nodiscard]] TargetNoteSelection
selectTargetNote(const std::shared_ptr<const score::Score>& score);
