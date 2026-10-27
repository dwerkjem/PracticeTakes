#pragma once

#include <string>

#include "Pitch.h"

// Rendering a notated pitch as a readable name (no JUCE dependency).
//
// Separate from Pitch.h on purpose: that header is `constexpr` throughout and
// includes no `<string>`, and keeping it arithmetic-only is worth more than
// having everything pitch-shaped in one file.
//
// This belongs to the model rather than to a consumer because the model is what
// keeps the spelling. `noteNameForMidi` in src/features/analysis/tuner is not
// the same function and cannot be: it starts from a MIDI number, which cannot
// tell F-sharp from G-flat, and it is private to a tool that a src/features/*
// tool may not reach into. Deriving a name from the spelling is the model's job,
// or every consumer writes its own and they disagree about enharmonics.
namespace score
{
// The accidental part of a name: "#", "b", "##", "bb", or empty.
//
// ASCII rather than the Unicode sharp and flat signs, because the tuner already
// shows "C#4" and one application should spell a note one way. An alteration
// beyond a double is rendered as the repeated sign rather than clamped -- the
// importer already rounds microtones to a semitone with a diagnostic, so an
// extreme value means the file really said so, and hiding it would be worse
// than showing it.
[[nodiscard]] inline std::string accidentalName(int alter)
{
    if (alter == 0)
    {
        return {};
    }

    const char sign = alter > 0 ? '#' : 'b';

    return std::string(static_cast<std::size_t>(alter > 0 ? alter : -alter), sign);
}

[[nodiscard]] inline std::string stepName(Step step)
{
    switch (step)
    {
    case Step::c:
        return "C";
    case Step::d:
        return "D";
    case Step::e:
        return "E";
    case Step::f:
        return "F";
    case Step::g:
        return "G";
    case Step::a:
        return "A";
    case Step::b:
        return "B";
    }

    return "C";
}

// The pitch as written: step, accidental, and scientific octave -- "C4", "F#3",
// "Bbb2". Octave -1 renders as "C-1", matching MIDI 0, which is what the tuner
// already shows for it.
//
// Two pitches that sound the same but are spelled differently name differently,
// which is the whole reason this takes a Pitch rather than a MIDI number.
[[nodiscard]] inline std::string spelledName(const Pitch& pitch)
{
    return stepName(pitch.step) + accidentalName(pitch.alter) + std::to_string(pitch.octave);
}
} // namespace score
