#pragma once

#include <string>
#include <vector>

#include "MusicalTime.h"
#include "ScoreAttributes.h"
#include "ScoreEvent.h"

// A measure and the voices inside it.
namespace score
{
// One voice's events within a measure, in ascending onset order.
//
// MusicXML writes voices with a cursor -- <backup> and <forward> move the write
// position within the measure -- so events are *not* in time order in the file.
// Resolving that cursor is the importer's job; by the time a Voice exists, the
// order here is the musical order. Invariant 2 is what says so.
struct Voice
{
    // MusicXML's voice number within the part. Not an index: a part may use
    // voices 1 and 5 with nothing between.
    int number = 1;

    std::vector<ScoreEvent> events;
};

// Repeat and jump markings, captured but *not* interpreted.
//
// Per decision 3 the model is as-written: measures appear once, in source
// order, and nothing here expands a repeat into a playback order. The data is
// kept faithfully so that whichever change first needs to *hear* a repeat can
// derive a playback order from it without re-importing, and so the renderer can
// draw the barlines and ending brackets.
struct RepeatMarkings
{
    // A left-facing repeat barline: |: at the start of this measure.
    bool repeatStart = false;

    // A right-facing repeat barline: :| at the end of this measure.
    bool repeatEnd = false;

    // How many times the repeat is taken, when the file says. MusicXML's
    // `times` attribute; 0 means unstated.
    int repeatTimes = 0;

    // Ending (volta) numbers this measure belongs to: {1} for a first ending,
    // {1, 2} for a bracket marked "1. 2.". Empty when the measure is not under
    // an ending bracket.
    std::vector<int> endingNumbers;

    // Jump and marker directions as written, uninterpreted: "D.C. al Fine",
    // "D.S. al Coda", "Fine", "Coda", "Segno". Strings rather than an enum
    // because the set is open and the MVP does not act on any of them.
    std::vector<std::string> jumps;
};

[[nodiscard]] inline bool isEmpty(const RepeatMarkings& markings) noexcept
{
    return !markings.repeatStart && !markings.repeatEnd && markings.endingNumbers.empty() &&
           markings.jumps.empty();
}

struct Measure
{
    // The measure number *as printed in the file*, kept as a string because
    // MusicXML measure numbers are not integers: a pickup is "0", a split bar
    // is "12a", and some exporters leave it blank. Diagnostics quote this, so a
    // user can find the bar on the page.
    std::string printedNumber;

    // Zero-based position in the part's measure list. This is the identity a
    // consumer should index by; `printedNumber` is for display.
    std::size_t index = 0;

    // Absolute start of this measure in the score, in model ticks. Invariant 1
    // requires measure n to start at the same tick in every part.
    Tick start = 0;

    // Ticks this measure *should* hold, from the prevailing time signature.
    // What invariant 7 checks each voice against.
    Tick nominalDuration = 0;

    // MusicXML's <measure implicit="yes">: a pickup (anacrusis). Such a measure
    // legitimately holds less than its time signature, which is why invariant 7
    // exempts it.
    bool isPickup = false;

    // Clef/key/time taking effect at the start of this measure. Usually empty.
    AttributeChanges attributes;

    RepeatMarkings repeats;

    // Tempo and dynamic markings attached within this measure.
    std::vector<Direction> directions;

    std::vector<Voice> voices;
};
} // namespace score
