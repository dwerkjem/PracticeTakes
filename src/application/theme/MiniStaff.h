#pragma once

#include "../../platform/pitch/TargetChoice.h"
#include "../../platform/score/Pitch.h"
#include "MusicFont.h"

#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <span>
#include <vector>

// A small five-line staff with a clef and a notehead or two on it: the Note
// View's staff, Now & Next's, and the target picker's grand staff (one call
// per staff).
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

    // The music font, to draw the clef's own glyph at the staff's start in
    // the lines' colour, in place of its name. None writes the name instead
    // (`label`). Every staff the application draws passes one.
    const MusicFont* font = nullptr;

    // The key signature after the clef, in fifths (sharps positive, flats
    // negative), drawn with the font. A head's accidental is then shown only
    // where it differs from the key, with a natural where the key would alter
    // it (pitch::keyInForce gives the key a tool is in).
    int keyFifths = 0;
};

// Where a key signature's accidentals sit on a staff, in steps from the middle
// line, in the order they are written: F C G D A E B for sharps, B E A D G C F
// for flats, at the conventional height for the clef.
[[nodiscard]] std::vector<int> miniStaffKeySteps(int keyFifths, pitch::StaffClef clef);

// The alteration a key gives a letter: +1, -1, or 0.
[[nodiscard]] int miniStaffKeyAlter(score::Step step, int keyFifths);

// "Treble", "Bass" or "Tenor" (the treble clef an octave down, as a tenor
// part is written), in the interface's language.
[[nodiscard]] juce::String miniStaffClefName(pitch::StaffClef clef);

// The line spacing a staff in `area` is drawn with.
[[nodiscard]] float miniStaffSpacing(juce::Rectangle<float> area, const MiniStaffStyle& style);

// How far from the area's left the clef and the key signature reach, so the
// heads go right of them: nothing without a font.
[[nodiscard]] float miniStaffClefWidth(juce::Rectangle<float> area, const MiniStaffStyle& style);

// The staff position under `y` in a staff drawn across `area`, in steps from
// the middle line (up is positive), to the nearest line or space.
[[nodiscard]] int
miniStaffStepsAt(juce::Rectangle<float> area, const MiniStaffStyle& style, float y);

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
