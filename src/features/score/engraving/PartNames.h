#pragma once

#include <string>

// What a part is called beside its staves (no JUCE dependency).
//
// A score with several parts names each one at the start of its staves, so a
// reader can tell the voice from the piano: the full name on the first line of
// music, the short form on every line after, where the full one would cost a
// column of width on every system for something the reader already knows.
namespace engraving
{
// The full name to print, or empty when the part has none worth printing. Only
// surrounding whitespace is removed: a file's name is its own, capitalisation
// included.
[[nodiscard]] std::string displayedPartName(const std::string& name);

// The short form to print on later systems.
//
// The file's own abbreviation when it gives one. Otherwise one is derived from
// the name, because most files give none -- the bundled sample's "Voice" and
// "Piano" carry no abbreviation at all -- and a later system with no name is
// the thing this exists to avoid:
//
// - a name that is an instrument or voice a reader knows ("Piano", "Violoncello",
//   "Soprano", in English, German or Italian) takes its conventional
//   abbreviation ("Pno.", "Vc.", "S."), keeping any trailing number
//   ("Violin 2" -> "Vln. 2");
// - a name of four characters or fewer is already short, and stays as it is;
// - anything else takes its first three letters and a full stop.
//
// Empty when there is no name to shorten.
[[nodiscard]] std::string
abbreviatedPartName(const std::string& name, const std::string& abbreviation);
} // namespace engraving
