#include "PlaybackEvents.h"

#include <algorithm>

namespace transport
{
namespace
{
// The note a tie continues into, if the file's reference holds.
[[nodiscard]] const score::Note* noteAt(const score::Part& part, const score::NoteRef& ref)
{
    if (ref.measureIndex >= part.measures.size())
    {
        return nullptr;
    }
    const score::Measure& measure = part.measures[ref.measureIndex];
    if (ref.voiceIndex >= measure.voices.size())
    {
        return nullptr;
    }
    const auto& events = measure.voices[ref.voiceIndex].events;
    if (ref.eventIndex >= events.size() || ref.noteIndex >= events[ref.eventIndex].notes.size())
    {
        return nullptr;
    }
    return &events[ref.eventIndex].notes[ref.noteIndex];
}

// Where a note and everything tied after it ends, in ticks. The chain is
// bounded by the number of notes in the part, so a malformed cycle ends.
[[nodiscard]] score::Tick
endOfTies(const score::Part& part, const score::NoteRef& first, std::size_t limit)
{
    score::NoteRef ref = first;
    score::Tick end = 0;
    for (std::size_t step = 0; step <= limit; ++step)
    {
        const score::Measure& measure = part.measures[ref.measureIndex];
        const score::ScoreEvent& event = measure.voices[ref.voiceIndex].events[ref.eventIndex];
        end = measure.start + event.onset + event.duration;

        const score::Note* note = noteAt(part, ref);
        if (note == nullptr || !note->tiedTo.has_value() || noteAt(part, *note->tiedTo) == nullptr)
        {
            break;
        }
        ref = *note->tiedTo;
    }
    return end;
}
} // namespace

std::vector<PlaybackTrack> playbackTracksOf(const score::Score& score)
{
    std::vector<PlaybackTrack> tracks;
    for (std::size_t part = 0; part < score.parts.size(); ++part)
    {
        const score::Part& source = score.parts[part];
        for (int staff = 1; staff <= std::max(1, source.staffCount); ++staff)
        {
            if (tracks.size() >= maximumPlaybackTracks)
            {
                return tracks;
            }
            tracks.push_back(
                PlaybackTrack{part, staff, source.midiProgram.value_or(defaultMidiProgram)});
        }
    }
    return tracks;
}

void playbackEventsOf(const score::Score& score, const ClockTable& table, PlaybackScore& into)
{
    into.tracks = playbackTracksOf(score);
    into.events.clear();
    if (table.empty())
    {
        return;
    }

    std::size_t firstTrack = 0;
    for (std::size_t partIndex = 0; partIndex < score.parts.size(); ++partIndex)
    {
        const score::Part& part = score.parts[partIndex];
        const int staves = std::max(1, part.staffCount);

        std::size_t notesInPart = 0;
        for (const score::Measure& measure : part.measures)
        {
            for (const score::Voice& voice : measure.voices)
            {
                notesInPart += voice.events.size();
            }
        }

        for (std::size_t m = 0; m < part.measures.size(); ++m)
        {
            const score::Measure& measure = part.measures[m];
            for (std::size_t v = 0; v < measure.voices.size(); ++v)
            {
                const auto& events = measure.voices[v].events;
                for (std::size_t e = 0; e < events.size(); ++e)
                {
                    const score::ScoreEvent& event = events[e];
                    if (!score::isPitched(event) || event.isGrace || event.isCue)
                    {
                        continue;
                    }
                    const std::size_t track =
                        firstTrack +
                        static_cast<std::size_t>(std::clamp(event.staff, 1, staves) - 1);
                    if (track >= into.tracks.size())
                    {
                        continue;
                    }

                    const score::Tick onset = measure.start + event.onset;
                    for (std::size_t n = 0; n < event.notes.size(); ++n)
                    {
                        const score::Note& note = event.notes[n];
                        if (note.tiedFrom.has_value())
                        {
                            continue;
                        }
                        const int key = note.sounding.midiNoteNumber;
                        if (key < 0 || key > 127)
                        {
                            continue;
                        }
                        const score::Tick end =
                            endOfTies(part, score::NoteRef{m, v, e, n}, notesInPart);
                        const Frame on = frameOfTick(table, static_cast<double>(onset));
                        const Frame off =
                            std::max(on + 1, frameOfTick(table, static_cast<double>(end)));
                        const auto trackIndex = static_cast<std::uint8_t>(track);
                        const auto keyNumber = static_cast<std::uint8_t>(key);
                        into.events.push_back(PlaybackEvent{on, trackIndex, keyNumber, true, 0});
                        into.events.push_back(PlaybackEvent{off, trackIndex, keyNumber, false, 0});
                    }
                }
            }
        }
        firstTrack += static_cast<std::size_t>(staves);
    }

    std::sort(
        into.events.begin(), into.events.end(),
        [](const PlaybackEvent& left, const PlaybackEvent& right)
        {
            if (left.frame != right.frame)
            {
                return left.frame < right.frame;
            }
            if (left.on != right.on)
            {
                return !left.on;
            }
            if (left.track != right.track)
            {
                return left.track < right.track;
            }
            return left.key < right.key;
        });
}
} // namespace transport
