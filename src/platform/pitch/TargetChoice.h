#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "../score/Pitch.h"

// The ways of naming a target note that need no display: typing its name,
// clicking a staff position, and stepping from the current one (no JUCE
// dependency). openspec/changes/archive/2026-11-08-target-note-picker/design.md, decision 2.
namespace pitch
{
// A pitch with a MIDI number in range, or nothing.
[[nodiscard]] inline std::optional<score::Pitch> spelled(score::Step step, int alter, int octave)
{
    const score::Pitch result = score::pitchFromSpelling(step, alter, octave);
    if (result.midiNoteNumber < 0 || result.midiNoteNumber > 127)
    {
        return std::nullopt;
    }
    return result;
}

// "F#4", "Bb3", "c4", "E♭5", "G 4": a letter, any sharps or flats, an octave
// from -1 to 9. Nothing for anything else.
[[nodiscard]] inline std::optional<score::Pitch> parseNoteName(std::string_view text)
{
    std::string compact;
    for (const char character : text)
    {
        if (std::isspace(static_cast<unsigned char>(character)) == 0)
        {
            compact.push_back(character);
        }
    }
    if (compact.empty())
    {
        return std::nullopt;
    }

    static constexpr std::string_view letters = "cdefgab";
    const auto letter =
        letters.find(static_cast<char>(std::tolower(static_cast<unsigned char>(compact.front()))));
    if (letter == std::string_view::npos)
    {
        return std::nullopt;
    }

    std::size_t at = 1;
    int alter = 0;
    static constexpr std::string_view sharpSign = "\xE2\x99\xAF";
    static constexpr std::string_view flatSign = "\xE2\x99\xAD";
    while (at < compact.size())
    {
        const std::string_view rest = std::string_view(compact).substr(at);
        if (rest.front() == '#')
        {
            ++alter;
            ++at;
        }
        else if (rest.front() == 'b')
        {
            --alter;
            ++at;
        }
        else if (rest.substr(0, sharpSign.size()) == sharpSign)
        {
            ++alter;
            at += sharpSign.size();
        }
        else if (rest.substr(0, flatSign.size()) == flatSign)
        {
            --alter;
            at += flatSign.size();
        }
        else
        {
            break;
        }
    }
    if (alter < -2 || alter > 2)
    {
        return std::nullopt;
    }

    const std::string octaveText = compact.substr(at);
    if (octaveText.empty() || octaveText.size() > 2)
    {
        return std::nullopt;
    }
    const bool negative = octaveText.front() == '-';
    const std::string digits = negative ? octaveText.substr(1) : octaveText;
    if (digits.size() != 1 || std::isdigit(static_cast<unsigned char>(digits.front())) == 0)
    {
        return std::nullopt;
    }
    const int octave = (negative ? -1 : 1) * (digits.front() - '0');
    if (octave < -1 || octave > 9)
    {
        return std::nullopt;
    }
    return spelled(static_cast<score::Step>(letter), alter, octave);
}

enum class StaffClef
{
    treble,
    bass,

    // Treble an octave down: tenor voices and guitar.
    trebleOctaveDown
};

// The pitch on a clef's middle line, counted in letters (octave times seven
// plus the letter): B4 for treble, D3 for bass, B3 for treble an octave down.
[[nodiscard]] constexpr int middleLineLetters(StaffClef clef) noexcept
{
    switch (clef)
    {
    case StaffClef::treble:
        return (4 * 7) + 6;
    case StaffClef::bass:
        return (3 * 7) + 1;
    case StaffClef::trebleOctaveDown:
        return (3 * 7) + 6;
    }
    return (4 * 7) + 6;
}

// How many lines-and-spaces above a clef's middle line a pitch sits (below,
// when negative). The accidental does not move it.
[[nodiscard]] constexpr int staffStepsOf(const score::Pitch& pitch, StaffClef clef) noexcept
{
    return (pitch.octave * 7) + static_cast<int>(pitch.step) - middleLineLetters(clef);
}

// The pitch on a staff `steps` lines-and-spaces above its middle line (below,
// when negative), with `alter` from the accidental chosen.
[[nodiscard]] inline std::optional<score::Pitch>
pitchAtStaffPosition(StaffClef clef, int steps, int alter)
{
    const int middle = middleLineLetters(clef);
    const int letters = middle + steps;
    if (letters < 0)
    {
        return std::nullopt;
    }
    return spelled(static_cast<score::Step>(letters % 7), alter, letters / 7);
}

// A MIDI number spelled with sharps (as a key has no spelling of its own), or
// with flats.
[[nodiscard]] inline std::optional<score::Pitch> spellMidi(int midi, bool sharps)
{
    static constexpr int sharpSteps[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    static constexpr int sharpAlters[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    static constexpr int flatSteps[12] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
    static constexpr int flatAlters[12] = {0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0};
    if (midi < 0 || midi > 127)
    {
        return std::nullopt;
    }
    const int pitchClass = midi % 12;
    const int octave = (midi / 12) - 1;
    return sharps ? spelled(
                        static_cast<score::Step>(sharpSteps[pitchClass]), sharpAlters[pitchClass],
                        octave)
                  : spelled(
                        static_cast<score::Step>(flatSteps[pitchClass]), flatAlters[pitchClass],
                        octave);
}

// A heard pitch, which has no spelling of its own, spelled as a piece in
// `keyFifths` would spell it: with flats in a flat key and sharps otherwise. A
// piece in F minor names its A flat A flat, not G sharp.
[[nodiscard]] inline std::optional<score::Pitch> spellForKey(int midi, int keyFifths)
{
    return spellMidi(midi, keyFifths >= 0);
}

// A semitone up, spelled with a sharp, or down, spelled with a flat.
[[nodiscard]] inline std::optional<score::Pitch>
stepBySemitone(const score::Pitch& from, int direction)
{
    return spellMidi(from.midiNoteNumber + (direction > 0 ? 1 : -1), direction > 0);
}

// The alteration a major key in `fifths` (-7 to 7) gives a letter.
[[nodiscard]] constexpr int keyAlteration(score::Step step, int fifths) noexcept
{
    // Sharps are added F C G D A E B; flats B E A D G C F.
    constexpr score::Step sharpOrder[7] = {
        score::Step::f, score::Step::c, score::Step::g, score::Step::d,
        score::Step::a, score::Step::e, score::Step::b};
    for (int index = 0; index < 7; ++index)
    {
        if (fifths > index && sharpOrder[index] == step)
        {
            return 1;
        }
        if (-fifths > index && sharpOrder[6 - index] == step)
        {
            return -1;
        }
    }
    return 0;
}

// The next letter up or down in a major key of `fifths`, spelled as the key
// spells it.
[[nodiscard]] inline std::optional<score::Pitch>
stepInKey(const score::Pitch& from, int direction, int fifths)
{
    int letter = static_cast<int>(from.step) + (from.octave * 7) + (direction > 0 ? 1 : -1);
    if (letter < 0)
    {
        return std::nullopt;
    }
    const auto step = static_cast<score::Step>(letter % 7);
    return spelled(step, keyAlteration(step, fifths), letter / 7);
}
} // namespace pitch
