#pragma once

#include <string>

#include "Pitch.h"

// How a musician names a pitch (no JUCE dependency):
// openspec/changes/archive/2026-11-09-internationalization, decision 5.
//
// Two choices, each a reader's own and independent of the interface language
// and of each other. The naming convention is the letters: "H" is not the
// German translation of "B" but a different system, in which B means B flat.
// The octave numbering is where the numbers start: middle C is C4 to a
// scientific reader, C3 on a Yamaha keyboard, and c' to Helmholtz.
//
// Not a catalogue entry, for the reason above, and so not translated: a German
// interface can read English letters, and an English one H.
namespace score
{
enum class NoteNaming
{
    // C D E F G A B, with # and b: what this application has always shown.
    englishDutch,

    // C D E F G A H, B for B flat, and -is / -es for sharps and flats.
    german,

    // Do Re Mi Fa Sol La Si, fixed: Do is always C.
    romance
};

enum class OctaveNumbering
{
    // Middle C is C4; MIDI 0 is C-1.
    scientific,

    // Middle C is C3.
    yamaha,

    // Middle C is c' (c with one prime); the octave below it lower case, the
    // two below that capitals, then capitals with commas.
    helmholtz
};

struct PitchNamingStyle
{
    NoteNaming naming = NoteNaming::englishDutch;
    OctaveNumbering octaves = OctaveNumbering::scientific;

    [[nodiscard]] bool operator==(const PitchNamingStyle&) const = default;
};

namespace naming_detail
{
[[nodiscard]] inline char letterOf(Step step) noexcept
{
    constexpr char letters[] = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
    return letters[static_cast<int>(step)];
}

[[nodiscard]] inline std::string repeated(const std::string& unit, int times)
{
    std::string text;
    for (int index = 0; index < times; ++index)
    {
        text += unit;
    }
    return text;
}

// "#", "b", "##", "bb": ASCII, as the tuner has always shown it.
[[nodiscard]] inline std::string signs(int alter)
{
    return alter > 0 ? repeated("#", alter) : repeated("b", -alter);
}

// The German name without an octave: Cis, Es, As, B, H, Heses.
[[nodiscard]] inline std::string germanName(Step step, int alter)
{
    // B flat is B and B natural is H; every other letter keeps its own.
    if (step == Step::b)
    {
        if (alter == -1)
        {
            return "B";
        }
        if (alter < -1)
        {
            return "H" + repeated("es", -alter);
        }
        return "H" + repeated("is", alter);
    }

    std::string name(1, letterOf(step));
    if (alter > 0)
    {
        return name + repeated("is", alter);
    }
    if (alter < 0)
    {
        // A vowel takes "s" rather than "es": Es, As; Eses, Ases.
        const bool vowel = step == Step::e || step == Step::a;
        return name + (vowel ? "s" : "es") + repeated("es", -alter - 1);
    }
    return name;
}

[[nodiscard]] inline std::string romanceName(Step step, int alter)
{
    constexpr const char* syllables[] = {"Do", "Re", "Mi", "Fa", "Sol", "La", "Si"};
    return std::string(syllables[static_cast<int>(step)]) + signs(alter);
}

[[nodiscard]] inline std::string lowered(std::string name)
{
    if (!name.empty() && name.front() >= 'A' && name.front() <= 'Z')
    {
        name.front() = static_cast<char>(name.front() - 'A' + 'a');
    }
    return name;
}

// c' c'' c''' as primes: ′ ″ ‴, and repeated beyond.
[[nodiscard]] inline std::string primes(int count)
{
    switch (count)
    {
    case 0:
        return {};
    case 1:
        return "\xE2\x80\xB2";
    case 2:
        return "\xE2\x80\xB3";
    case 3:
        return "\xE2\x80\xB4";
    default:
        return repeated("\xE2\x80\xB2", count);
    }
}
} // namespace naming_detail

// The name of a letter and its alteration, with no octave: "F#", "Fis", "Fa#".
[[nodiscard]] inline std::string
pitchClassName(Step step, int alter, NoteNaming naming = NoteNaming::englishDutch)
{
    switch (naming)
    {
    case NoteNaming::german:
        return naming_detail::germanName(step, alter);
    case NoteNaming::romance:
        return naming_detail::romanceName(step, alter);
    case NoteNaming::englishDutch:
        break;
    }
    return std::string(1, naming_detail::letterOf(step)) + naming_detail::signs(alter);
}

// A spelled pitch, named as `style` names it: "C4", "Bb3"; "H3" and "B3" in
// German; "Do4"; "c′" in Helmholtz.
[[nodiscard]] inline std::string pitchName(const Pitch& pitch, PitchNamingStyle style = {})
{
    const std::string name = pitchClassName(pitch.step, pitch.alter, style.naming);
    switch (style.octaves)
    {
    case OctaveNumbering::scientific:
        return name + std::to_string(pitch.octave);
    case OctaveNumbering::yamaha:
        return name + std::to_string(pitch.octave - 1);
    case OctaveNumbering::helmholtz:
        if (pitch.octave >= 3)
        {
            return naming_detail::lowered(name) + naming_detail::primes(pitch.octave - 3);
        }
        return name + naming_detail::repeated(",", 2 - pitch.octave);
    }
    return name + std::to_string(pitch.octave);
}
} // namespace score
