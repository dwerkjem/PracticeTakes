#pragma once

#include "../../application/theme/SmuflGlyphs.h"
#include "engraving/Accidental.h"
#include "engraving/NoteValue.h"

#include <optional>

// The glyphs the engraving's own kinds are drawn with. Beside the Score View
// rather than with the glyph table in application/theme, because the kinds are
// the engraver's: the table and the font are shared with every staff the
// application draws, and these are not.
namespace smufl
{
[[nodiscard]] Glyph noteheadFor(engraving::NoteheadKind head);

[[nodiscard]] Glyph restFor(engraving::WrittenValue value);

// Nothing for `none`, which is a note that draws no accidental.
[[nodiscard]] std::optional<Glyph> accidentalFor(engraving::AccidentalKind kind);
} // namespace smufl
