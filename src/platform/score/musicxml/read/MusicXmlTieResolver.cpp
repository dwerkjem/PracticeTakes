#include "MusicXmlTieResolver.h"

#include <map>
#include <utility>

namespace score::musicxml
{
namespace
{
ScoreEvent& eventAt(Part& part, const EventRef& reference)
{
    return part.measures[reference.measureIndex]
        .voices[reference.voiceIndex]
        .events[reference.eventIndex];
}
} // namespace

void resolveTies(
    ReadContext& context,
    Part& part,
    const std::vector<std::vector<PendingVoice>>& pendingByMeasure)
{
    // A tie start waiting for its stop, keyed by voice number and **sounding**
    // pitch rather than by spelling: a file may legitimately tie a G-sharp to
    // an A-flat across a barline, and refusing that would drop a real tie.
    std::map<std::pair<int, int>, EventRef> pendingStarts;

    for (std::size_t measureIndex = 0; measureIndex < pendingByMeasure.size(); ++measureIndex)
    {
        const std::vector<PendingVoice>& voices = pendingByMeasure[measureIndex];

        for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
        {
            const PendingVoice& voice = voices[voiceIndex];

            for (std::size_t eventIndex = 0; eventIndex < voice.events.size(); ++eventIndex)
            {
                const PendingEvent& pending = voice.events[eventIndex];

                if (pending.event.pitches.empty())
                {
                    continue;
                }

                const std::pair<int, int> key{
                    voice.number, pending.event.pitches.front().midiNoteNumber};
                const EventRef self{measureIndex, voiceIndex, eventIndex};

                if (pending.tieStop)
                {
                    const auto start = pendingStarts.find(key);

                    if (start == pendingStarts.end())
                    {
                        context.diagnostics.addRepair(
                            locationOf(part, part.measures[measureIndex], voice.number), "tie",
                            "A tie ends on a note that nothing ties to, so the tie was dropped.");
                    }
                    else
                    {
                        eventAt(part, start->second).tiedTo = self;
                        eventAt(part, self).tiedFrom = start->second;
                        pendingStarts.erase(start);
                    }
                }

                if (pending.tieStart)
                {
                    pendingStarts[key] = self;
                }
            }
        }
    }

    for (const auto& [key, reference] : pendingStarts)
    {
        context.diagnostics.addRepair(
            locationOf(part, part.measures[reference.measureIndex], key.first), "tie",
            "A tie starts on a note and never ends, so the tie was dropped.");
    }
}
} // namespace score::musicxml
