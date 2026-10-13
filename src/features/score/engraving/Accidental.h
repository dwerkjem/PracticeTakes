#pragma once

#include <string>

// The accidental signs a renderer draws (no JUCE dependency).
//
// Its own header because two unrelated things need it and neither should depend
// on the other: a key signature's accidentals (`KeySignatureLayout`) and the
// ones drawn on individual notes (`MeasureAccidentals`).
namespace engraving
{
enum class AccidentalKind
{
    // Nothing drawn. Distinct from `natural`, which is a sign.
    none,

    natural,
    sharp,
    flat,
    doubleSharp,
    doubleFlat
};

// The sign for an alteration in semitones.
//
// An alteration beyond a double is clamped to the double sign rather than
// dropped: the importer already rounds microtones to a semitone with a
// diagnostic, so a value this large means the file said so, and drawing nothing
// would hide a note that is three semitones from where it looks.
// `score::accidentalName` takes the same view one layer down.
[[nodiscard]] constexpr AccidentalKind accidentalKindFor(int alter) noexcept
{
    if (alter >= 2)
    {
        return AccidentalKind::doubleSharp;
    }

    if (alter == 1)
    {
        return AccidentalKind::sharp;
    }

    if (alter == 0)
    {
        return AccidentalKind::natural;
    }

    if (alter == -1)
    {
        return AccidentalKind::flat;
    }

    return AccidentalKind::doubleFlat;
}

[[nodiscard]] inline std::string nameOf(AccidentalKind kind)
{
    switch (kind)
    {
    case AccidentalKind::none:
        return "";
    case AccidentalKind::natural:
        return "natural";
    case AccidentalKind::sharp:
        return "sharp";
    case AccidentalKind::flat:
        return "flat";
    case AccidentalKind::doubleSharp:
        return "double sharp";
    case AccidentalKind::doubleFlat:
        return "double flat";
    }

    return "";
}
} // namespace engraving
