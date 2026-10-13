#include "Accidentals.h"

#include <algorithm>
#include <array>

namespace score::engraving
{
namespace
{
// The order accidentals are added to a key signature. Fixed by the circle of
// fifths, not by the font and not by taste.
constexpr std::array<Step, 7> sharpOrder{
    Step::f, Step::c, Step::g, Step::d, Step::a, Step::e, Step::b};

constexpr std::array<Step, 7> flatOrder{
    Step::b, Step::e, Step::a, Step::d, Step::g, Step::c, Step::f};

[[nodiscard]] int positionIn(const std::array<Step, 7>& order, Step step) noexcept
{
    const auto found = std::find(order.begin(), order.end(), step);

    return static_cast<int>(std::distance(order.begin(), found));
}
} // namespace

int keyAlterationFor(Step step, const KeySignature& key) noexcept
{
    if (key.fifths > 0)
    {
        return positionIn(sharpOrder, step) < key.fifths ? 1 : 0;
    }

    if (key.fifths < 0)
    {
        return positionIn(flatOrder, step) < -key.fifths ? -1 : 0;
    }

    return 0;
}

std::optional<Glyph> accidentalGlyphFor(int alter) noexcept
{
    switch (alter)
    {
    case -2:
        return Glyph::accidentalDoubleFlat;
    case -1:
        return Glyph::accidentalFlat;
    case 0:
        return Glyph::accidentalNatural;
    case 1:
        return Glyph::accidentalSharp;
    case 2:
        return Glyph::accidentalDoubleSharp;
    default:
        break;
    }

    // Microtones are rounded at import with a diagnostic (Pitch.h), so an alter
    // outside this range should not reach a renderer. Drawing nothing is the
    // right failure: a wrong accidental is worse than a missing one.
    return std::nullopt;
}

void AccidentalState::setKey(const KeySignature& key)
{
    keySignature = key;
    measureAccidentals.clear();
}

void AccidentalState::beginMeasure()
{
    measureAccidentals.clear();
}

int AccidentalState::alterationInForceFor(const Pitch& pitch) const noexcept
{
    // An earlier accidental in this measure wins, and only for its own octave.
    for (const MeasureAccidental& accidental : measureAccidentals)
    {
        if (accidental.step == pitch.step && accidental.octave == pitch.octave)
        {
            return accidental.alter;
        }
    }

    return keyAlterationFor(pitch.step, keySignature);
}

std::optional<Glyph> AccidentalState::accidentalFor(const Pitch& pitch)
{
    if (alterationInForceFor(pitch) == pitch.alter)
    {
        return std::nullopt;
    }

    const std::optional<Glyph> glyph = accidentalGlyphFor(pitch.alter);

    if (!glyph.has_value())
    {
        // Nothing drawn, so nothing goes into force either -- recording an
        // alteration whose accidental was never printed would suppress the next
        // note's accidental and leave the bar unreadable.
        return std::nullopt;
    }

    const auto existing = std::find_if(
        measureAccidentals.begin(), measureAccidentals.end(),
        [&pitch](const MeasureAccidental& accidental)
        { return accidental.step == pitch.step && accidental.octave == pitch.octave; });

    if (existing != measureAccidentals.end())
    {
        existing->alter = pitch.alter;
    }
    else
    {
        measureAccidentals.push_back(MeasureAccidental{pitch.step, pitch.octave, pitch.alter});
    }

    return glyph;
}
} // namespace score::engraving
