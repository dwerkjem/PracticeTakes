#pragma once

#include <map>
#include <string>
#include <unordered_set>

// The boundary of the supported MusicXML subset, as data.
//
// The three tables here are what makes the boundary testable and reviewable
// rather than scattered through the reading code as `if` statements. Every
// element the importer meets falls into exactly one of four cases:
//
//  1. Read into the model.
//  2. Recognised, deliberately dropped -- `unsupportedConstructs()` names it and
//     says why, once, with a count (task 7.1).
//  3. Known and intentionally stepped over (engraving detail we neither read
//     nor need to complain about) -- in `knownElements()`.
//  4. Never heard of -- aggregated by name into one diagnostic with an
//     occurrence count (task 7.2).
//
// None of the four can fail an import (task 7.3).
namespace score::musicxml
{
// Element name to the explanation a user gets when it is dropped. Keyed by name
// so a construct is reported once however many times it occurs.
[[nodiscard]] const std::map<std::string, std::string>& unsupportedConstructs();

// Every element the importer knows about, whether it reads it or steps over it.
// Anything outside this set is case 4 above. Keeping the set explicit is how a
// dialect this importer has never seen announces itself instead of vanishing.
[[nodiscard]] const std::unordered_set<std::string>& knownElements();

// Whether `name` is one of the dynamic markings MusicXML spells as an element
// inside <dynamics> -- "mf", "sfz", and the rest.
[[nodiscard]] bool isDynamicMarking(const std::string& name);
} // namespace score::musicxml
