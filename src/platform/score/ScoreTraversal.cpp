#include "ScoreTraversal.h"

#include <algorithm>

namespace score
{
std::vector<MeasureEventIndex> measureEventOrder(const Measure& measure)
{
    std::vector<MeasureEventIndex> order;

    std::size_t total = 0;

    for (const Voice& voice : measure.voices)
    {
        total += voice.events.size();
    }

    order.reserve(total);

    for (std::size_t voiceIndex = 0; voiceIndex < measure.voices.size(); ++voiceIndex)
    {
        const Voice& voice = measure.voices[voiceIndex];

        for (std::size_t eventIndex = 0; eventIndex < voice.events.size(); ++eventIndex)
        {
            order.push_back(MeasureEventIndex{voiceIndex, eventIndex});
        }
    }

    // Stable, so that two voices sharing a number -- which the model does not
    // forbid -- keep their order in the measure rather than depending on the
    // sort's internals. Within one voice the events are already in ascending
    // onset order by invariant 2, and stability preserves that too, so a chord
    // and a grace note at the same onset stay as the file wrote them.
    std::stable_sort(
        order.begin(), order.end(),
        [&measure](const MeasureEventIndex& lhs, const MeasureEventIndex& rhs)
        {
            const Voice& leftVoice = measure.voices[lhs.voiceIndex];
            const Voice& rightVoice = measure.voices[rhs.voiceIndex];

            const Tick leftOnset = leftVoice.events[lhs.eventIndex].onset;
            const Tick rightOnset = rightVoice.events[rhs.eventIndex].onset;

            if (leftOnset != rightOnset)
            {
                return leftOnset < rightOnset;
            }

            return leftVoice.number < rightVoice.number;
        });

    return order;
}
} // namespace score
