#include "NoteValue.h"

namespace score::engraving
{
namespace
{
// A dotted value is base * (2 - 2^-dots), so three dots is the most that stays
// legible and the most any notation this draws uses.
constexpr int maximumDots = 3;

[[nodiscard]] Glyph noteheadForFlags(int flags) noexcept
{
    if (flags <= -2)
    {
        return Glyph::noteheadWhole;
    }

    if (flags == -1)
    {
        return Glyph::noteheadHalf;
    }

    return Glyph::noteheadBlack;
}

// Rests have their own glyph per value, where noteheads share one from the
// quarter down.
[[nodiscard]] Glyph restForFlags(int flags) noexcept
{
    switch (flags)
    {
    case -2:
        return Glyph::restWhole;
    case -1:
        return Glyph::restHalf;
    case 0:
        return Glyph::restQuarter;
    case 1:
        return Glyph::rest8th;
    default:
        break;
    }

    return Glyph::rest16th;
}

// Ticks in the undotted value with this many flags. Counting flags downward
// through zero is what lets one integer name the whole range: -2 is a whole
// note, 0 a quarter, 2 a sixteenth.
[[nodiscard]] Tick ticksForFlags(int flags) noexcept
{
    Tick ticks = ticksPerQuarterNote;

    for (int step = 0; step < flags; ++step)
    {
        ticks /= 2;
    }

    for (int step = flags; step < 0; ++step)
    {
        ticks *= 2;
    }

    return ticks;
}
} // namespace

NoteValue noteValueFor(Tick duration, const TupletRatio& tuplet) noexcept
{
    NoteValue value;

    // Three in the time of two means each lasts two thirds, so the written
    // value is the duration scaled back up.
    const Tick actual = tuplet.actual > 0 ? static_cast<Tick>(tuplet.actual) : 1;
    const Tick normal = tuplet.normal > 0 ? static_cast<Tick>(tuplet.normal) : 1;

    const Tick written = duration > 0 ? (duration * actual) / normal : 0;

    if (written <= 0)
    {
        // A grace note has no duration of its own (invariant 3). It is written
        // as an eighth, which is what every engraver sets one as, and that is a
        // decision rather than a failure -- so `exact` stays true.
        value.flags = 1;
        value.notehead = noteheadForFlags(1);
        value.rest = restForFlags(1);

        return value;
    }

    // The largest undotted value that fits.
    int flags = -2;

    while (flags < maximumFlags && ticksForFlags(flags) > written)
    {
        ++flags;
    }

    const Tick base = ticksForFlags(flags);

    value.flags = flags < 0 ? 0 : flags;
    value.notehead = noteheadForFlags(flags);
    value.rest = restForFlags(flags);

    if (base > written)
    {
        // Shorter than anything this draws. The shortest value is the closest
        // honest answer, and the flag is what says it is not the right one.
        value.exact = false;

        return value;
    }

    // Dots, by addition rather than by solving: the remainder either is half
    // the base, or three quarters of it, or seven eighths, or it is not a
    // dotted value at all.
    Tick remainder = written - base;
    Tick increment = base;

    for (int dots = 0; dots < maximumDots; ++dots)
    {
        if (remainder == 0)
        {
            return value;
        }

        increment /= 2;

        if (increment <= 0 || remainder < increment)
        {
            break;
        }

        remainder -= increment;
        ++value.dots;
    }

    value.exact = remainder == 0;

    return value;
}
} // namespace score::engraving
