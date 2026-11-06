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

// How much room a sign takes, in staff spaces, measured from its origin: SMuFL
// puts an accidental's origin at its left edge, on the line or space of the
// note it modifies. `above` and `below` are how far it reaches up and down from
// there -- a flat's stem rises well above its note and barely drops below it,
// which is why a flat over a sharp can share a column a sixth apart and a sharp
// over a flat cannot.
//
// This is what lets the layout stack a chord's accidentals and stay font-free.
// MusicFontTests checks every row against the embedded Bravura, the same way it
// pins `noteheadWidth`, so a font change cannot quietly leave the two apart.
struct AccidentalMetrics
{
    double width = 0.0;
    double above = 0.0;
    double below = 0.0;
};

[[nodiscard]] constexpr AccidentalMetrics accidentalMetricsFor(AccidentalKind kind) noexcept
{
    switch (kind)
    {
    case AccidentalKind::none:
        return {};
    case AccidentalKind::natural:
        return {0.672, 1.364, 1.34};
    case AccidentalKind::sharp:
        return {0.996, 1.4, 1.392};
    case AccidentalKind::flat:
        return {0.904, 1.756, 0.7};
    case AccidentalKind::doubleSharp:
        return {0.988, 0.508, 0.5};
    case AccidentalKind::doubleFlat:
        return {1.644, 1.748, 0.7};
    }

    return {};
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
