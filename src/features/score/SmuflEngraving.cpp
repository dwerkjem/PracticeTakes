#include "SmuflEngraving.h"

namespace smufl
{
Glyph noteheadFor(engraving::NoteheadKind head)
{
    switch (head)
    {
    case engraving::NoteheadKind::breve:
        return Glyph::noteheadDoubleWhole;
    case engraving::NoteheadKind::whole:
        return Glyph::noteheadWhole;
    case engraving::NoteheadKind::half:
        return Glyph::noteheadHalf;
    case engraving::NoteheadKind::filled:
        break;
    }

    return Glyph::noteheadBlack;
}

Glyph restFor(engraving::WrittenValue value)
{
    switch (value)
    {
    case engraving::WrittenValue::breve:
        return Glyph::restDoubleWhole;
    case engraving::WrittenValue::whole:
        return Glyph::restWhole;
    case engraving::WrittenValue::half:
        return Glyph::restHalf;
    case engraving::WrittenValue::quarter:
        return Glyph::restQuarter;
    case engraving::WrittenValue::eighth:
        return Glyph::rest8th;
    case engraving::WrittenValue::sixteenth:
        return Glyph::rest16th;
    case engraving::WrittenValue::thirtySecond:
        return Glyph::rest32nd;
    case engraving::WrittenValue::sixtyFourth:
        return Glyph::rest64th;
    case engraving::WrittenValue::hundredTwentyEighth:
        break;
    }

    return Glyph::rest128th;
}

std::optional<Glyph> accidentalFor(engraving::AccidentalKind kind)
{
    switch (kind)
    {
    case engraving::AccidentalKind::flat:
        return Glyph::accidentalFlat;
    case engraving::AccidentalKind::natural:
        return Glyph::accidentalNatural;
    case engraving::AccidentalKind::sharp:
        return Glyph::accidentalSharp;
    case engraving::AccidentalKind::doubleSharp:
        return Glyph::accidentalDoubleSharp;
    case engraving::AccidentalKind::doubleFlat:
        return Glyph::accidentalDoubleFlat;
    case engraving::AccidentalKind::none:
        break;
    }

    return std::nullopt;
}
} // namespace smufl
