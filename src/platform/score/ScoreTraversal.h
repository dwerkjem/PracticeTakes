#pragma once

#include <cstddef>
#include <type_traits>
#include <vector>

#include "Measure.h"
#include "MusicalTime.h"
#include "Score.h"

// Visiting a part's events in musical order.
//
// Finding 3 of docs/development/architecture/score-model-findings.md: getting
// from a Score to its notes is parts -> measures -> voices -> events, skipping
// what should be skipped and comparing onsets *across* voices because a
// measure's voices are independent timelines. That walk is about forty lines,
// the Target Note tool wrote it once, and the renderer, the cursor and playback
// would each have written it again. Four slightly different definitions of "the
// next note" in one application is the failure this exists to prevent.
//
// Two rules are stated here rather than left to each caller, because a caller
// that chooses differently is not obviously wrong until two of them disagree:
//
//  - **Order is by absolute onset**, ascending, across measures and across the
//    voices within a measure.
//  - **Ties go to the lower `Voice::number`.** That is the rule
//    `selectTargetNote` already uses; it is repeated here rather than replaced
//    so the application has one answer. Note that `Voice::number` is not an
//    index -- a part may use voices 1 and 5 with nothing between -- so this is a
//    comparison of the file's own numbering, not of positions in a list.
//
// No JUCE, like the rest of the model.
namespace score
{
// Where one event sits within its measure.
struct MeasureEventIndex
{
    std::size_t voiceIndex = 0;
    std::size_t eventIndex = 0;
};

// Every event of a measure, ordered by ascending onset and then by ascending
// `Voice::number`.
//
// Separate from the traversal template below so that the ordering rule -- the
// part worth asserting -- is an ordinary function a test can call directly,
// rather than something reachable only through a callback.
[[nodiscard]] std::vector<MeasureEventIndex> measureEventOrder(const Measure& measure);

// One visited event, with the context the model otherwise makes the caller
// carry.
//
// References rather than values: a visit is handed to a callback and never
// outlives it, and copying a `ScoreEvent` to look at it would copy its notes
// and its lyrics.
struct EventVisit
{
    const Measure& measure;
    const Voice& voice;
    const ScoreEvent& event;

    // Absolute position in the score, already summed. Finding 4: the sum
    // `measure.start + event.onset` is correct in bar 1 and silently wrong
    // after it, which is the worst possible failure shape for something a test
    // fixture usually exercises in bar 1.
    Tick absoluteOnset = 0;

    std::size_t measureIndex = 0;
    std::size_t voiceIndex = 0;
    std::size_t eventIndex = 0;
};

// Visit every event of a part in musical order.
//
// The visitor may return `void`, or `bool` to stop early by returning false --
// the renderer wants the first form and a search wants the second, and
// supporting both here is cheaper than every caller wrapping one in the other.
//
// A template rather than a `std::function` so that the common case allocates
// nothing and inlines; the ordering work it delegates to `measureEventOrder`
// does allocate, which is fine on the message thread and irrelevant anywhere
// else because the audio thread may not touch a Score at all (Score.h).
template <typename Visitor> void forEachEvent(const Part& part, Visitor&& visit)
{
    for (std::size_t measureIndex = 0; measureIndex < part.measures.size(); ++measureIndex)
    {
        const Measure& measure = part.measures[measureIndex];

        for (const MeasureEventIndex& position : measureEventOrder(measure))
        {
            const Voice& voice = measure.voices[position.voiceIndex];
            const ScoreEvent& event = voice.events[position.eventIndex];

            const EventVisit visited{
                measure,
                voice,
                event,
                absoluteOnset(measure, event),
                measureIndex,
                position.voiceIndex,
                position.eventIndex};

            if constexpr (std::is_void_v<std::invoke_result_t<Visitor, const EventVisit&>>)
            {
                visit(visited);
            }
            else
            {
                if (!visit(visited))
                {
                    return;
                }
            }
        }
    }
}
} // namespace score
