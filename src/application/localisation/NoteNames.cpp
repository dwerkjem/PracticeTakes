#include "NoteNames.h"

#include "../../platform/pitch/TargetChoice.h"

namespace l10n
{
namespace
{
score::PitchNamingStyle& chosenStyle()
{
    static score::PitchNamingStyle style;
    return style;
}

pitch::SpellingPreference& chosenSpelling()
{
    static pitch::SpellingPreference preference;
    return preference;
}
} // namespace

void setSpelling(pitch::SpellingPreference preference)
{
    chosenSpelling() = preference;
}

pitch::SpellingPreference spelling()
{
    return chosenSpelling();
}

std::optional<score::Pitch> spellHeard(int midi, int keyFifths)
{
    return pitch::spellForKey(midi, keyFifths, chosenSpelling().flats);
}

void setNoteNaming(score::PitchNamingStyle style)
{
    chosenStyle() = style;
}

score::PitchNamingStyle noteNaming()
{
    return chosenStyle();
}

juce::String noteName(const score::Pitch& pitch)
{
    return juce::String::fromUTF8(score::pitchName(pitch, chosenStyle()).c_str());
}

juce::String noteName(std::string_view canonical)
{
    if (const auto pitch = pitch::parseNoteName(canonical))
    {
        return noteName(*pitch);
    }
    return juce::String::fromUTF8(std::string(canonical).c_str());
}

juce::String heardNoteName(int midi, int keyFifths)
{
    if (const auto spelled = spellHeard(midi, keyFifths))
    {
        return noteName(*spelled);
    }
    return "--";
}
} // namespace l10n
