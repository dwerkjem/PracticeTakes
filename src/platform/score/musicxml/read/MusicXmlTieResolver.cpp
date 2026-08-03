#include "MusicXmlTieResolver.h"

#include <map>
#include <utility>

namespace score::musicxml
{
namespace
{
Note& noteAt(Part& part, const NoteRef& reference)
{
    return part.measures[reference.measureIndex]
        .voices[reference.voiceIndex]
        .events[reference.eventIndex]
        .notes[reference.noteIndex];
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
    //
    // Keyed per note rather than per event, which is what lets a chord tie some
    // of its notes and not others. Matching on the event's first pitch -- the
    // obvious shortcut -- silently drops a tie whenever the chord changes shape
    // across the barline, and silently sustains the whole chord when only one
    // note was held.
    std::map<std::pair<int, int>, NoteRef> pendingStarts;

    for (std::size_t measureIndex = 0; measureIndex < pendingByMeasure.size(); ++measureIndex)
    {
        const std::vector<PendingVoice>& voices = pendingByMeasure[measureIndex];

        for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
        {
            const PendingVoice& voice = voices[voiceIndex];

            for (std::size_t eventIndex = 0; eventIndex < voice.events.size(); ++eventIndex)
            {
                const PendingEvent& pending = voice.events[eventIndex];

                for (std::size_t noteIndex = 0; noteIndex < pending.event.notes.size(); ++noteIndex)
                {
                    const PendingTie& tie = pending.noteTies[noteIndex];

                    if (!tie.start && !tie.stop)
                    {
                        continue;
                    }

                    const std::pair<int, int> key{
                        voice.number, pending.event.notes[noteIndex].sounding.midiNoteNumber};
                    const NoteRef self{measureIndex, voiceIndex, eventIndex, noteIndex};

                    // Stop first, then start. A note in the middle of a chain
                    // carries both, and it must close the chain behind it
                    // before opening the one ahead -- otherwise it would match
                    // against itself.
                    if (tie.stop)
                    {
                        const auto start = pendingStarts.find(key);

                        if (start == pendingStarts.end())
                        {
                            context.diagnostics.addRepair(
                                locationOf(part, part.measures[measureIndex], voice.number), "tie",
                                "A tie ends on a note that nothing ties to, so the tie was "
                                "dropped.");
                        }
                        else
                        {
                            noteAt(part, start->second).tiedTo = self;
                            noteAt(part, self).tiedFrom = start->second;
                            pendingStarts.erase(start);
                        }
                    }

                    if (tie.start)
                    {
                        pendingStarts[key] = self;
                    }
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
