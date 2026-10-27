#include "MeasureAccidentals.h"

#include "KeySignatureLayout.h"

#include <array>

namespace engraving
{
namespace
{
// The order the circle of fifths alters the steps in -- the same order the
// signature is drawn in, which is why it is the same list.
constexpr std::array<score::Step, 7> sharpOrder{
    score::Step::f, score::Step::c, score::Step::g, score::Step::d,
    score::Step::a, score::Step::e, score::Step::b};

constexpr std::array<score::Step, 7> flatOrder{
    score::Step::b, score::Step::e, score::Step::a, score::Step::d,
    score::Step::g, score::Step::c, score::Step::f};
} // namespace

int keyAlterationFor(const score::KeySignature& key, score::Step step)
{
    const int count = keySignatureAccidentalCount(key);

    if (count == 0)
    {
        return 0;
    }

    const auto& order = key.fifths > 0 ? sharpOrder : flatOrder;

    for (int index = 0; index < count; ++index)
    {
        if (order.at(static_cast<std::size_t>(index)) == step)
        {
            return key.fifths > 0 ? 1 : -1;
        }
    }

    return 0;
}

MeasureAccidentals::MeasureAccidentals(const score::KeySignature& key)
{
    for (const score::Step step :
         {score::Step::c, score::Step::d, score::Step::e, score::Step::f, score::Step::g,
          score::Step::a, score::Step::b})
    {
        const int alteration = keyAlterationFor(key, step);

        if (alteration != 0)
        {
            fromKey.emplace(step, alteration);
        }
    }
}

int MeasureAccidentals::alterationInForce(score::Step step, int octave) const
{
    const auto drawn = drawnInThisMeasure.find(StepInOctave{octave, step});

    if (drawn != drawnInThisMeasure.end())
    {
        return drawn->second;
    }

    const auto key = fromKey.find(step);

    return key == fromKey.end() ? 0 : key->second;
}

AccidentalKind MeasureAccidentals::accidentalFor(const score::Pitch& pitch, bool isTieContinuation)
{
    if (isTieContinuation)
    {
        return AccidentalKind::none;
    }

    if (pitch.alter == alterationInForce(pitch.step, pitch.octave))
    {
        return AccidentalKind::none;
    }

    drawnInThisMeasure[StepInOctave{pitch.octave, pitch.step}] = pitch.alter;

    // `accidentalKindFor` maps an alteration of zero to a natural, which is
    // exactly right here: the only way to reach this line with a zero
    // alteration is that something had altered this step, and cancelling it is
    // what a natural sign is for.
    return accidentalKindFor(pitch.alter);
}
} // namespace engraving
