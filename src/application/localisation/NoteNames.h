#pragma once

#include "../../platform/score/PitchNaming.h"
#include "Localisation.h"

#include <string_view>

// How notes are named on screen: the reader's naming convention and octave
// numbering, chosen in Settings and independent of the interface language
// (openspec/changes/archive/2026-11-09-internationalization, decision 5). The rule is
// score::pitchName; this holds the choice, as l10n::current() holds the
// language. Message thread.
namespace l10n
{
void setNoteNaming(score::PitchNamingStyle style);

[[nodiscard]] score::PitchNamingStyle noteNaming();

// A spelled pitch, in the reader's style.
[[nodiscard]] juce::String noteName(const score::Pitch& pitch);

// A canonical name ("Bb3", as a target is stored), in the reader's style; as
// written when it is not a note name.
[[nodiscard]] juce::String noteName(std::string_view canonical);

// A heard or played pitch, which has no spelling of its own, spelled as a
// piece in `keyFifths` would spell it, in the reader's style.
[[nodiscard]] juce::String heardNoteName(int midi, int keyFifths = 0);
} // namespace l10n
