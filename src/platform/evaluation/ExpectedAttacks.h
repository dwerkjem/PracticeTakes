#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include "../score/Score.h"

// The attacks a part asks for: one moment at each distinct onset tick, with
// every note that starts there (no JUCE dependency).
// openspec/changes/archive/2026-11-08-onset-timing/design.md, decision 1.
namespace evaluation
{
struct ExpectedNote
{
    score::NoteRef ref;

    // The sounding pitch, which is what a keyboard plays and a microphone hears.
    int midiNoteNumber = 60;
};

struct ExpectedAttack
{
    // Absolute, in score ticks.
    score::Tick tick = 0;
    std::size_t measureIndex = 0;
    std::vector<ExpectedNote> notes;
};

// The part's attacks in time order. A note tied from an earlier note is not an
// attack -- it is not played again -- so an event whose every note continues a
// tie makes none. Rests and grace notes make none either: a rest has nothing to
// attack, and a grace note's moment is the performer's to place. Chords, and
// voices starting together, are one moment.
[[nodiscard]] inline std::vector<ExpectedAttack>
expectedAttacksOf(const score::Score& score, std::size_t partIndex)
{
    std::map<score::Tick, ExpectedAttack> byTick;
    if (partIndex >= score.parts.size())
    {
        return {};
    }

    const score::Part& part = score.parts.at(partIndex);
    for (std::size_t measureIndex = 0; measureIndex < part.measures.size(); ++measureIndex)
    {
        const score::Measure& measure = part.measures.at(measureIndex);
        for (std::size_t voice = 0; voice < measure.voices.size(); ++voice)
        {
            const auto& events = measure.voices.at(voice).events;
            for (std::size_t event = 0; event < events.size(); ++event)
            {
                const score::ScoreEvent& candidate = events.at(event);
                if (!score::isPitched(candidate) || candidate.isGrace)
                {
                    continue;
                }
                for (std::size_t note = 0; note < candidate.notes.size(); ++note)
                {
                    const score::Note& written = candidate.notes.at(note);
                    if (written.tiedFrom.has_value())
                    {
                        continue;
                    }
                    const score::Tick tick = measure.start + candidate.onset;
                    ExpectedAttack& attack = byTick[tick];
                    attack.tick = tick;
                    attack.measureIndex = measureIndex;
                    attack.notes.push_back(ExpectedNote{
                        score::NoteRef{measureIndex, voice, event, note},
                        written.sounding.midiNoteNumber});
                }
            }
        }
    }

    std::vector<ExpectedAttack> attacks;
    attacks.reserve(byTick.size());
    for (auto& [tick, attack] : byTick)
    {
        attacks.push_back(std::move(attack));
    }
    return attacks;
}
} // namespace evaluation
