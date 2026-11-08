#include "TargetNoteSelection.h"

#include "Measure.h"
#include "PitchNames.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
// What the walk finds in one measure, before it is described.
struct Candidate
{
    const score::ScoreEvent* event = nullptr;
    std::size_t voiceIndex = 0;
    std::size_t eventIndex = 0;
    int voiceNumber = 1;
};

[[nodiscard]] bool isTargetable(const score::ScoreEvent& event)
{
    return isPitched(event) && !event.isGrace;
}

// The earliest targetable event in a measure, across all of its voices.
//
// Earliest onset wins; a tie goes to the lower voice number. Scanning every
// voice rather than taking the first voice that has a note matters because the
// voices of a measure are independent timelines: a piano part writing a bass
// voice that enters on beat one and a treble voice that rests until beat three
// would otherwise report the treble note as the score's first.
[[nodiscard]] std::optional<Candidate> earliestIn(const score::Measure& measure)
{
    std::optional<Candidate> best;

    for (std::size_t voiceIndex = 0; voiceIndex < measure.voices.size(); ++voiceIndex)
    {
        const score::Voice& voice = measure.voices[voiceIndex];

        for (std::size_t eventIndex = 0; eventIndex < voice.events.size(); ++eventIndex)
        {
            const score::ScoreEvent& event = voice.events[eventIndex];

            if (!isTargetable(event))
            {
                continue;
            }

            const bool better =
                !best.has_value() || event.onset < best->event->onset ||
                (event.onset == best->event->onset && voice.number < best->voiceNumber);

            if (better)
            {
                best = Candidate{&event, voiceIndex, eventIndex, voice.number};
            }

            // Events within a voice are in ascending onset order (invariant 2),
            // so nothing later in this voice can beat what was just taken.
            break;
        }
    }

    return best;
}

// Work title, then movement title, then the first part's name.
//
// A file that names none of them still has to be identifiable -- "which score
// is this" is the first question the tool answers -- so the last resort is a
// stated placeholder rather than a blank heading.
[[nodiscard]] std::string titleOf(const score::Score& score)
{
    if (!score.metadata.workTitle.empty())
    {
        return score.metadata.workTitle;
    }

    if (!score.metadata.movementTitle.empty())
    {
        return score.metadata.movementTitle;
    }

    if (!score.parts.empty() && !score.parts.front().name.empty())
    {
        return score.parts.front().name;
    }

    return "Untitled score";
}
} // namespace

bool isMonophonic(const score::Part& part)
{
    for (const score::Measure& measure : part.measures)
    {
        // Each voice's sounding spans, to check across voices.
        std::vector<std::pair<score::Tick, score::Tick>> spans;
        std::vector<std::size_t> voiceOf;
        for (std::size_t voice = 0; voice < measure.voices.size(); ++voice)
        {
            for (const score::ScoreEvent& event : measure.voices[voice].events)
            {
                if (!isTargetable(event))
                {
                    continue;
                }
                if (event.notes.size() > 1)
                {
                    return false;
                }
                spans.emplace_back(event.onset, event.onset + event.duration);
                voiceOf.push_back(voice);
            }
        }
        for (std::size_t first = 0; first < spans.size(); ++first)
        {
            for (std::size_t second = first + 1; second < spans.size(); ++second)
            {
                const bool overlap = spans[first].first < spans[second].second &&
                                     spans[second].first < spans[first].second;
                if (overlap && voiceOf[first] != voiceOf[second])
                {
                    return false;
                }
            }
        }
    }
    return true;
}

TargetNoteSelection selectTargetNote(const std::shared_ptr<const score::Score>& score)
{
    if (score == nullptr)
    {
        return {TargetNoteOutcome::noScoreOpen, {}, std::nullopt};
    }

    TargetNoteSelection selection;
    selection.outcome = TargetNoteOutcome::noPitchedNote;
    selection.scoreTitle = titleOf(*score);

    if (score->parts.empty())
    {
        return selection;
    }

    bool sawPolyphony = false;
    for (std::size_t partIndex = 0; partIndex < score->parts.size(); ++partIndex)
    {
        const score::Part& part = score->parts[partIndex];
        const bool hasNote = std::any_of(
            part.measures.begin(), part.measures.end(),
            [](const score::Measure& measure) { return earliestIn(measure).has_value(); });
        if (!hasNote)
        {
            continue;
        }
        if (!isMonophonic(part))
        {
            sawPolyphony = true;
            continue;
        }

        for (const score::Measure& measure : part.measures)
        {
            const std::optional<Candidate> candidate = earliestIn(measure);

            if (!candidate.has_value())
            {
                continue;
            }

            const score::ScoreEvent& event = *candidate->event;
            const score::Note& note = event.notes.front();

            // Absolute position is the measure's start plus the event's
            // measure-relative onset: the model keeps onsets relative so a
            // measure can be examined and repaired on its own, which leaves the
            // sum to the consumer.
            const score::Tick start = measure.start + event.onset;

            TargetNote target;
            target.location =
                score::NoteRef{measure.index, candidate->voiceIndex, candidate->eventIndex, 0};
            target.partIndex = partIndex;
            target.partId = part.id;
            target.partName = part.name.empty() ? part.id : part.name;
            target.staff = event.staff;
            target.voiceNumber = candidate->voiceNumber;
            target.printedBarNumber = measure.printedNumber;
            target.soundingName = score::spelledName(note.sounding);
            target.soundingMidiNoteNumber = note.sounding.midiNoteNumber;
            target.writtenName = score::spelledName(note.written);
            target.soundsAsWritten = soundsAsWritten(note);
            target.startSeconds = score->tempoMap.tickToSeconds(start);
            target.durationSeconds =
                score->tempoMap.tickToSeconds(start + event.duration) - target.startSeconds;

            selection.outcome = TargetNoteOutcome::found;
            selection.note = target;

            return selection;
        }
    }

    if (sawPolyphony)
    {
        selection.outcome = TargetNoteOutcome::onlyPolyphonic;
    }
    return selection;
}
