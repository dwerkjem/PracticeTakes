#include "PartTimeline.h"

#include <algorithm>

namespace score
{
namespace
{
[[nodiscard]] std::string lyricOf(const ScoreEvent& event)
{
    for (const LyricSyllable& syllable : event.lyrics)
    {
        if (syllable.verse != 1)
        {
            continue;
        }
        const bool goesOn = syllable.position == SyllabicPosition::begin ||
                            syllable.position == SyllabicPosition::middle;
        return goesOn ? syllable.text + "-" : syllable.text;
    }
    return {};
}

// Every note of the event continues a tie: nothing is struck.
[[nodiscard]] bool onlyContinues(const ScoreEvent& event)
{
    return std::all_of(
        event.notes.begin(), event.notes.end(),
        [](const Note& note) { return note.tiedFrom.has_value(); });
}

[[nodiscard]] bool samePitches(const std::vector<Pitch>& left, const std::vector<Pitch>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].midiNoteNumber != right[index].midiNoteNumber)
        {
            return false;
        }
    }
    return true;
}

void addRest(PartTimeline& timeline, Tick start, Tick end)
{
    if (end <= start)
    {
        return;
    }
    if (!timeline.empty() && !timeline.back().isNote() && timeline.back().end >= start)
    {
        timeline.back().end = std::max(timeline.back().end, end);
        return;
    }
    TimelineItem rest;
    rest.start = start;
    rest.end = end;
    timeline.push_back(rest);
}
} // namespace

PartTimeline timelineOf(const Score& source, std::size_t partIndex)
{
    PartTimeline timeline;
    if (partIndex >= source.parts.size())
    {
        return timeline;
    }

    const Part& part = source.parts[partIndex];
    Tick reached = part.measures.empty() ? 0 : part.measures.front().start;
    for (std::size_t measureIndex = 0; measureIndex < part.measures.size(); ++measureIndex)
    {
        const Measure& measure = part.measures[measureIndex];
        if (measure.voices.empty())
        {
            continue;
        }
        const auto& events = measure.voices.front().events;
        for (std::size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex)
        {
            const ScoreEvent& event = events[eventIndex];
            if (event.isGrace || event.isCue || event.duration <= 0)
            {
                continue;
            }
            const Tick start = measure.start + event.onset;
            const Tick end = start + event.duration;

            if (!isPitched(event))
            {
                addRest(timeline, start, end);
                reached = std::max(reached, end);
                continue;
            }

            // A gap the voice leaves unwritten is silence, like a rest.
            addRest(timeline, reached, start);

            if (onlyContinues(event) && !timeline.empty() && timeline.back().isNote())
            {
                timeline.back().end = std::max(timeline.back().end, end);
                timeline.back().tiedOver = true;
                reached = std::max(reached, end);
                continue;
            }

            TimelineItem note;
            note.kind = TimelineKind::note;
            note.start = start;
            note.end = end;
            note.ref = NoteRef{measureIndex, 0, eventIndex, 0};
            note.lyric = lyricOf(event);
            for (const Note& written : event.notes)
            {
                note.pitches.push_back(written.sounding);
            }
            std::sort(
                note.pitches.begin(), note.pitches.end(), [](const Pitch& left, const Pitch& right)
                { return left.midiNoteNumber < right.midiNoteNumber; });

            // Against the last note, and what came between it and this one.
            const auto lastNote = std::find_if(
                timeline.rbegin(), timeline.rend(),
                [](const TimelineItem& item) { return item.isNote(); });
            note.repeated =
                lastNote != timeline.rend() && samePitches(lastNote->pitches, note.pitches);
            note.phraseStart = timeline.empty() || !timeline.back().isNote();
            timeline.push_back(std::move(note));
            reached = std::max(reached, end);
        }
    }
    return timeline;
}

std::optional<std::size_t> itemAt(const PartTimeline& timeline, double tick)
{
    const auto after = std::upper_bound(
        timeline.begin(), timeline.end(), tick, [](double value, const TimelineItem& item)
        { return value < static_cast<double>(item.start); });
    if (after == timeline.begin())
    {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(std::distance(timeline.begin(), after) - 1);
    if (tick >= static_cast<double>(timeline[index].end))
    {
        return std::nullopt;
    }
    return index;
}

std::optional<std::size_t> itemAfter(const PartTimeline& timeline, double tick)
{
    const auto after = std::upper_bound(
        timeline.begin(), timeline.end(), tick, [](double value, const TimelineItem& item)
        { return value < static_cast<double>(item.start); });
    if (after == timeline.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(timeline.begin(), after));
}
} // namespace score
