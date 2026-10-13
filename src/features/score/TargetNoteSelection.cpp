#include "TargetNoteSelection.h"

#include "platform/score/Measure.h"
#include "platform/score/PitchNames.h"

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

    const score::Part& part = score->parts.front();

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
        // measure-relative onset: the model keeps onsets relative so a measure
        // can be examined and repaired on its own, which leaves the sum to the
        // consumer.
        const score::Tick start = measure.start + event.onset;

        TargetNote target;
        target.location =
            score::NoteRef{measure.index, candidate->voiceIndex, candidate->eventIndex, 0};
        target.partId = part.id;
        target.partName = part.name.empty() ? part.id : part.name;
        target.staff = event.staff;
        target.voiceNumber = candidate->voiceNumber;
        target.printedBarNumber = measure.printedNumber;
        target.soundingName = score::spelledName(note.sounding);
        target.soundingMidiNoteNumber = note.sounding.midiNoteNumber;
        target.writtenName = score::spelledName(note.written);
        target.soundsAsWritten = soundsAsWritten(note);
        target.isChordMember = event.notes.size() > 1;
        target.chordNoteCount = event.notes.size();
        target.startSeconds = score->tempoMap.tickToSeconds(start);
        target.durationSeconds =
            score->tempoMap.tickToSeconds(start + event.duration) - target.startSeconds;

        selection.outcome = TargetNoteOutcome::found;
        selection.note = target;

        return selection;
    }

    return selection;
}
