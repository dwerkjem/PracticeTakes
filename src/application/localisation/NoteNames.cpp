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
} // namespace

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
    if (const auto spelled = pitch::spellForKey(midi, keyFifths))
    {
        return noteName(*spelled);
    }
    return "--";
}
} // namespace l10n
