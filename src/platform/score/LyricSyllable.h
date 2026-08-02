#pragma once

#include <string>

// One syllable of sung text attached to a note (no JUCE dependency).
//
// This is the piece a singer-facing practice application exists for, so it is
// modelled properly rather than as a bare string: the renderer needs to know
// whether to draw a hyphen after the syllable ("be-" + "-gin") and whether to
// draw an extender line under a melisma, and neither is recoverable from the
// text alone.
namespace score
{
// MusicXML's <syllabic>: where this syllable sits within its word.
enum class SyllabicPosition
{
    // A whole word on one note.
    single,

    // First syllable of a multi-syllable word -- draw a hyphen after it.
    begin,

    // Interior syllable -- hyphens on both sides.
    middle,

    // Last syllable of the word -- hyphen before it, none after.
    end
};

struct LyricSyllable
{
    // MusicXML's verse `number` attribute. Verse 1 is the first line of text;
    // a hymn with four verses has four syllables on most notes.
    int verse = 1;

    SyllabicPosition position = SyllabicPosition::single;

    std::string text;

    // MusicXML's <extend>: this syllable is held across the notes that follow,
    // i.e. a melisma. The renderer draws an extender line; nothing else in the
    // MVP interprets it.
    bool extend = false;
};
} // namespace score
