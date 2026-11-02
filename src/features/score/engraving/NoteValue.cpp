#include "NoteValue.h"

#include <array>

namespace engraving
{
namespace
{
// Longest first, which is the order `writtenValueFor` has to try them in: a
// whole note is a half note's duration doubled, so a search that started short
// would match nothing a dot could not also explain.
constexpr std::array<WrittenValue, 9> allValues{
    WrittenValue::breve,        WrittenValue::whole,       WrittenValue::half,
    WrittenValue::quarter,      WrittenValue::eighth,      WrittenValue::sixteenth,
    WrittenValue::thirtySecond, WrittenValue::sixtyFourth, WrittenValue::hundredTwentyEighth};

constexpr int maximumDots = 2;
} // namespace

score::Tick ticksFor(WrittenValue value)
{
    switch (value)
    {
    case WrittenValue::breve:
        return score::ticksPerWholeNote * 2;
    case WrittenValue::whole:
        return score::ticksPerWholeNote;
    case WrittenValue::half:
        return score::ticksPerQuarterNote * 2;
    case WrittenValue::quarter:
        return score::ticksPerQuarterNote;
    case WrittenValue::eighth:
        return score::ticksPerQuarterNote / 2;
    case WrittenValue::sixteenth:
        return score::ticksPerQuarterNote / 4;
    case WrittenValue::thirtySecond:
        return score::ticksPerQuarterNote / 8;
    case WrittenValue::sixtyFourth:
        return score::ticksPerQuarterNote / 16;
    case WrittenValue::hundredTwentyEighth:
        return score::ticksPerQuarterNote / 32;
    }

    return score::ticksPerQuarterNote;
}

score::Tick ticksFor(const WrittenNoteValue& value)
{
    const score::Tick base = ticksFor(value.value);

    // A dot adds half of what precedes it: one dot is 3/2, two is 7/4. The tick
    // base divides by 32 and then by 4 without a remainder for every value down
    // to a 128th, so this is exact integer arithmetic rather than a rounding.
    switch (value.dots)
    {
    case 1:
        return base * 3 / 2;
    case 2:
        return base * 7 / 4;
    default:
        return base;
    }
}

NoteheadKind noteheadFor(WrittenValue value)
{
    switch (value)
    {
    case WrittenValue::breve:
        return NoteheadKind::breve;
    case WrittenValue::whole:
        return NoteheadKind::whole;
    case WrittenValue::half:
        return NoteheadKind::half;
    default:
        return NoteheadKind::filled;
    }
}

int flagCountFor(WrittenValue value)
{
    switch (value)
    {
    case WrittenValue::eighth:
        return 1;
    case WrittenValue::sixteenth:
        return 2;
    case WrittenValue::thirtySecond:
        return 3;
    case WrittenValue::sixtyFourth:
        return 4;
    case WrittenValue::hundredTwentyEighth:
        return 5;
    default:
        return 0;
    }
}

bool hasStem(WrittenValue value)
{
    return value != WrittenValue::breve && value != WrittenValue::whole;
}

std::string nameOf(WrittenValue value)
{
    switch (value)
    {
    case WrittenValue::breve:
        return "breve";
    case WrittenValue::whole:
        return "whole note";
    case WrittenValue::half:
        return "half note";
    case WrittenValue::quarter:
        return "quarter note";
    case WrittenValue::eighth:
        return "eighth note";
    case WrittenValue::sixteenth:
        return "sixteenth note";
    case WrittenValue::thirtySecond:
        return "thirty-second note";
    case WrittenValue::sixtyFourth:
        return "sixty-fourth note";
    case WrittenValue::hundredTwentyEighth:
        return "hundred-twenty-eighth note";
    }

    return "note";
}

std::optional<WrittenNoteValue>
writtenValueFor(score::Tick duration, const score::TupletRatio& tuplet)
{
    if (duration <= 0)
    {
        return std::nullopt;
    }

    if (tuplet.actual <= 0 || tuplet.normal <= 0)
    {
        return std::nullopt;
    }

    // Undo the tuplet compression to get the duration the note is *written* as.
    // A triplet eighth lasts two thirds of an eighth; multiplying by 3/2 gives
    // the eighth back.
    const score::Tick scaled = duration * tuplet.actual;

    if (scaled % tuplet.normal != 0)
    {
        return std::nullopt;
    }

    const score::Tick written = scaled / tuplet.normal;

    for (const WrittenValue value : allValues)
    {
        for (int dots = 0; dots <= maximumDots; ++dots)
        {
            const WrittenNoteValue candidate{value, dots};

            if (ticksFor(candidate) == written)
            {
                return candidate;
            }
        }
    }

    return std::nullopt;
}

std::optional<WrittenNoteValue> writtenValueFor(const score::ScoreEvent& event)
{
    return writtenValueFor(event.duration, event.tuplet);
}
} // namespace engraving
