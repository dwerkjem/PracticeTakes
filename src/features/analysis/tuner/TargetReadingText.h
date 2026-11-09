#pragma once

#include "../../../application/localisation/Localisation.h"
#include "../../../application/localisation/NoteNames.h"
#include "../../../platform/pitch/TargetReading.h"

// What the tuner says about the note it hears against the target: "Target B3
// · 12 ¢ flat". The standing is worked out in the JUCE-free pitch layer; the
// words are the catalogue's.
namespace tuner
{
[[nodiscard]] inline juce::String
targetReadingText(const pitch::Target& target, const pitch::TargetReading& reading)
{
    const auto note = l10n::noteName(target.name);
    const auto amount = static_cast<std::int64_t>(reading.amount);
    switch (reading.standing)
    {
    case pitch::TargetStanding::unheard:
        return l10n::format("target.heading", {{"note", note}});
    case pitch::TargetStanding::onIt:
        return l10n::format("target.reading.on-it", {{"note", note}});
    case pitch::TargetStanding::sharp:
        return l10n::format(
            "target.reading.sharp",
            {{"note", note}, {"cents", l10n::number(static_cast<double>(amount), 0)}});
    case pitch::TargetStanding::flat:
        return l10n::format(
            "target.reading.flat",
            {{"note", note}, {"cents", l10n::number(static_cast<double>(amount), 0)}});
    case pitch::TargetStanding::above:
        return l10n::plural("target.reading.above", amount, {{"note", note}});
    case pitch::TargetStanding::below:
        return l10n::plural("target.reading.below", amount, {{"note", note}});
    }
    return l10n::format("target.heading", {{"note", note}});
}
} // namespace tuner
