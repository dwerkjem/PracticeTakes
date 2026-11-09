#pragma once

#include "../../platform/pitch/TargetChoice.h"
#include "../../platform/score/Pitch.h"

#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <span>
#include <vector>

// A small five-line staff with a notehead or two on it: the Note View's staff,
// Now & Next's, and the target picker's grand staff (one call per staff).
//
// It is not notation -- the Score View engraves that -- just enough of a staff
// to say where a pitch sits: the five lines, ledger lines out to the note, the
// head, and a sharp or flat beside it. Every tool that draws one draws it
// through here, so the three stay alike and a fix lands in all of them.

// One notehead on the staff.
struct MiniStaffNote
{
    // Spelled, since the letter decides the line: G#4 and Ab4 sit apart.
    score::Pitch pitch;

    // The head's centre, in the same coordinates as the staff's area.
    float x = 0.0f;
    juce::Colour colour;

    // Hollow draws an outline: the Note View's target, against the filled
    // note being sung.
    bool filled = true;
};

// How a tool's staff looks. The defaults are the Note View's.
struct MiniStaffStyle
{
    // The staff and ledger lines.
    juce::Colour lines;

    // The clef's name, just above the staff and clear of any ledger lines a
    // high note needs. None leaves the staff unlabelled, for a caller that
    // labels it itself (the picker labels its grand staff in the margin).
    std::optional<juce::Colour> label;
    float labelFontHeight = 11.0f;

    // The gap between two lines is a fourteenth of the area's height, held
    // between these: a staff fills a short area and stops growing in a tall
    // one. Equal values fix the spacing.
    float minSpacing = 5.0f;
    float maxSpacing = 10.0f;

    // In staff spaces: a head's width and height, and how far a ledger line
    // reaches either side of the head's centre.
    float headWidth = 1.3f;
    float headHeight = 1.0f;
    float ledgerReach = 1.0f;

    // Ledger lines in the note's colour rather than the staff's, so a faint
    // hover head brings faint ledgers with it (the picker).
    bool ledgersInNoteColour = false;

    // A sharp or flat to the left of a head whose pitch is altered.
    bool accidentals = true;
};

// "Treble", "Bass" or "Treble 8vb", in the interface's language.
[[nodiscard]] juce::String miniStaffClefName(pitch::StaffClef clef);

// The line spacing a staff in `area` is drawn with.
[[nodiscard]] float miniStaffSpacing(juce::Rectangle<float> area, const MiniStaffStyle& style);

// The ledger lines a note `steps` above the middle line needs, as steps from
// the middle line: none inside the staff, then every line out to the note
// ({6, 8} for 8 or 9 steps up; {-6} for 6 or 7 down).
[[nodiscard]] std::vector<int> miniStaffLedgerSteps(int steps);

// Draws the staff across `area`, inset four pixels each side, its middle line
// at the area's vertical centre, then each note in order (a later head is drawn
// over an earlier one).
void drawMiniStaff(
    juce::Graphics& graphics,
    juce::Rectangle<float> area,
    pitch::StaffClef clef,
    std::span<const MiniStaffNote> notes,
    const MiniStaffStyle& style);
