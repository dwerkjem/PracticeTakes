#pragma once

// Pitch with its notated spelling retained (no JUCE dependency).
//
// The model stores the spelling *and* the sounding MIDI number, always. Both,
// because neither derives from the other in the direction we need:
//
//  - The renderer (#32) needs step/alter/octave to pick a staff line and an
//    accidental. C-sharp and D-flat are the same sounding number but different
//    staff positions, so a model holding only the number cannot be engraved.
//  - Playback (#35-#38) needs the number. Recomputing it from the spelling in
//    every consumer duplicates the arithmetic and the chance of getting it
//    wrong.
//
// Invariant 5 is that the two agree, which is what `isConsistent` checks.
namespace score
{
// Diatonic step name, as MusicXML's <step> spells it.
enum class Step
{
    c,
    d,
    e,
    f,
    g,
    a,
    b
};

// Semitone offset of each natural step above C, in one octave.
[[nodiscard]] constexpr int semitonesAboveC(Step step) noexcept
{
    switch (step)
    {
    case Step::c:
        return 0;
    case Step::d:
        return 2;
    case Step::e:
        return 4;
    case Step::f:
        return 5;
    case Step::g:
        return 7;
    case Step::a:
        return 9;
    case Step::b:
        return 11;
    }

    return 0;
}

struct Pitch
{
    Step step = Step::c;

    // Chromatic alteration in semitones: -1 flat, +1 sharp, -2/+2 double, 0
    // natural. MusicXML's <alter> is a decimal and permits microtones (0.5 for
    // a quarter tone); those are outside the MVP subset and are rounded to the
    // nearest semitone with a diagnostic at import, so this stays an int.
    int alter = 0;

    // Scientific pitch notation, matching MusicXML's <octave>: middle C is
    // C4 == MIDI 60.
    int octave = 4;

    // Sounding pitch, MIDI note number. Stored rather than computed so that
    // playback never has to redo the arithmetic, and cross-checked against the
    // spelling by invariant 5.
    int midiNoteNumber = 60;
};

// The MIDI number a spelling sounds as. C4 is 60, so the octave is offset by 1.
[[nodiscard]] constexpr int midiNoteNumberFor(Step step, int alter, int octave) noexcept
{
    return ((octave + 1) * 12) + semitonesAboveC(step) + alter;
}

// Build a pitch from its spelling, deriving the sounding number so the two
// cannot disagree. This is the only constructor importers should use.
[[nodiscard]] constexpr Pitch pitchFromSpelling(Step step, int alter, int octave) noexcept
{
    return Pitch{step, alter, octave, midiNoteNumberFor(step, alter, octave)};
}

// Invariant 5: the stored sounding number matches the stored spelling.
//
// This can only fail if something built a Pitch by aggregate initialisation
// instead of `pitchFromSpelling`, which is exactly the mistake worth catching:
// a note that renders on one line and sounds as another is silent corruption.
[[nodiscard]] constexpr bool isConsistent(const Pitch& pitch) noexcept
{
    return pitch.midiNoteNumber == midiNoteNumberFor(pitch.step, pitch.alter, pitch.octave);
}

[[nodiscard]] constexpr bool operator==(const Pitch& lhs, const Pitch& rhs) noexcept
{
    return lhs.step == rhs.step && lhs.alter == rhs.alter && lhs.octave == rhs.octave &&
           lhs.midiNoteNumber == rhs.midiNoteNumber;
}

[[nodiscard]] constexpr bool operator!=(const Pitch& lhs, const Pitch& rhs) noexcept
{
    return !(lhs == rhs);
}

// Diatonic position of a step within an octave: C is 0, B is 6. Distinct from
// `semitonesAboveC`, which is the chromatic distance -- transposition needs
// both, because it moves a note by a number of *steps* and a number of
// *semitones* independently. That is how a written C becomes a B-flat rather
// than an A-sharp.
[[nodiscard]] constexpr int diatonicIndex(Step step) noexcept
{
    switch (step)
    {
    case Step::c:
        return 0;
    case Step::d:
        return 1;
    case Step::e:
        return 2;
    case Step::f:
        return 3;
    case Step::g:
        return 4;
    case Step::a:
        return 5;
    case Step::b:
        return 6;
    }

    return 0;
}

[[nodiscard]] constexpr Step stepFromDiatonicIndex(int index) noexcept
{
    switch (((index % 7) + 7) % 7)
    {
    case 1:
        return Step::d;
    case 2:
        return Step::e;
    case 3:
        return Step::f;
    case 4:
        return Step::g;
    case 5:
        return Step::a;
    case 6:
        return Step::b;
    default:
        return Step::c;
    }
}

// How far a part's written pitch sits from its sounding pitch -- MusicXML's
// <transpose>.
//
// Both numbers are needed and neither implies the other. `chromatic` alone
// gives the right sound and the wrong spelling: a B-flat clarinet is -2
// semitones, and applying that to a written C without `diatonic` yields
// A-sharp rather than B-flat -- same key, wrong staff line, wrong accidental.
struct Transposition
{
    // Steps to move the written spelling by. -1 for a B-flat instrument.
    int diatonic = 0;

    // Semitones to move the written pitch by. -2 for a B-flat instrument.
    int chromatic = 0;

    // Whole octaves on top of the above. -1 for a tenor saxophone, which sounds
    // a ninth below rather than a second.
    int octaveChange = 0;

    // Which staff of a multi-staff part this applies to. MusicXML allows a
    // per-staff transposition through <transpose number="...">.
    int staff = 1;
};

[[nodiscard]] constexpr bool isIdentity(const Transposition& transposition) noexcept
{
    return transposition.diatonic == 0 && transposition.chromatic == 0 &&
           transposition.octaveChange == 0;
}

// The pitch `written` actually sounds on an instrument transposing by
// `transposition`.
//
// Spelling and sound are moved separately, which is the whole reason MusicXML
// carries two numbers: the diatonic step decides the staff line, the chromatic
// interval decides the key, and the accidental is whatever reconciles them. A
// written C on a B-flat clarinet becomes B-flat, not A-sharp.
[[nodiscard]] constexpr Pitch
transposed(const Pitch& written, const Transposition& transposition) noexcept
{
    const int sounding =
        written.midiNoteNumber + transposition.chromatic + (12 * transposition.octaveChange);

    // Floor division, not truncation: a written C moved down one step has to
    // land on the B *below* it, and -1 / 7 truncates towards zero.
    const int totalStep = diatonicIndex(written.step) + transposition.diatonic;
    const int octaveShift = (totalStep >= 0 ? totalStep : totalStep - 6) / 7;

    const Step step = stepFromDiatonicIndex(totalStep);
    const int octave = written.octave + octaveShift + transposition.octaveChange;

    // Whatever accidental makes the chosen spelling sound the required pitch.
    const int alter = sounding - (((octave + 1) * 12) + semitonesAboveC(step));

    return Pitch{step, alter, octave, sounding};
}

// Whether two pitches sound the same, regardless of how they are spelled.
// Tie matching (invariant 4) uses this: a file may legitimately tie a G-sharp
// to an A-flat across a barline.
[[nodiscard]] constexpr bool soundsSameAs(const Pitch& lhs, const Pitch& rhs) noexcept
{
    return lhs.midiNoteNumber == rhs.midiNoteNumber;
}
} // namespace score
