#pragma once

#include <optional>
#include <string>

#include "platform/score/LyricSyllable.h"
#include "platform/score/Pitch.h"

// Turning MusicXML's text values into the model's types.
//
// Every one of these refuses rather than guesses. A `<duration>` of "3x" is not
// 3: a file that says something unreadable has to produce a diagnostic, and
// silently taking the prefix is how a wrong score gets imported without anyone
// noticing. That is why each returns an optional instead of a value with a
// fallback baked in -- the caller decides what a missing value means, because
// only the caller knows.
namespace score::musicxml
{
// Whole-number element or attribute text. Rejects trailing junk.
[[nodiscard]] std::optional<long long> parseInteger(const std::string& text);

// Decimal element or attribute text. Rejects trailing junk and non-finite
// values. MusicXML uses decimals for <alter> (microtones) and tempo.
[[nodiscard]] std::optional<double> parseDecimal(const std::string& text);

// MusicXML <step>: a single letter A through G.
[[nodiscard]] std::optional<Step> parseStep(const std::string& text);

// MusicXML <syllabic>. Anything unrecognised is `single`, which is the
// harmless reading: a syllable that stands alone draws no hyphens.
[[nodiscard]] SyllabicPosition parseSyllabic(const std::string& text);

// Quarter notes in one <metronome> beat unit, so that "half = 60" converts to
// 120 quarter notes per minute. `dots` is the number of <beat-unit-dot>
// elements; each adds half of what came before, so a dotted half is three
// quarters.
[[nodiscard]] double quarterNotesPerBeatUnit(const std::string& beatUnit, int dots);
} // namespace score::musicxml
