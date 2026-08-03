#include "ScoreInvariants.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace score
{
namespace
{
void addRepair(
    Score& score,
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    Diagnostic diagnostic;
    diagnostic.severity = DiagnosticSeverity::repaired;
    diagnostic.location = std::move(location);
    diagnostic.elementName = std::move(elementName);
    diagnostic.message = std::move(message);

    score.diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] DiagnosticLocation locate(const Part& part, const Measure& measure)
{
    DiagnosticLocation location;
    location.partId = part.id;
    location.measureNumber = measure.printedNumber;

    return location;
}

[[nodiscard]] DiagnosticLocation
locate(const Part& part, const Measure& measure, const Voice& voice)
{
    DiagnosticLocation location = locate(part, measure);
    location.voice = voice.number;

    return location;
}

// A whole-measure rest, used to pad a part that is short of measures.
[[nodiscard]] ScoreEvent wholeMeasureRest(Tick duration)
{
    ScoreEvent rest;
    rest.kind = EventKind::rest;
    rest.onset = 0;
    rest.duration = duration;

    return rest;
}
} // namespace

int enforceConsistentPitches(Score& score)
{
    int repairs = 0;

    for (Part& part : score.parts)
    {
        for (Measure& measure : part.measures)
        {
            for (Voice& voice : measure.voices)
            {
                for (ScoreEvent& event : voice.events)
                {
                    for (Note& note : event.notes)
                    {
                        Pitch& pitch = note.pitch;

                        if (isConsistent(pitch))
                        {
                            continue;
                        }

                        // The spelling is what the engraver wrote; the number
                        // is derived. So the spelling wins.
                        pitch.midiNoteNumber =
                            midiNoteNumberFor(pitch.step, pitch.alter, pitch.octave);

                        ++repairs;
                        addRepair(
                            score, locate(part, measure, voice), "pitch",
                            "sounding pitch disagreed with its notated spelling; "
                            "recomputed from the spelling");
                    }
                }
            }
        }
    }

    return repairs;
}

int enforceNonNegativeDurations(Score& score)
{
    int repairs = 0;

    for (Part& part : score.parts)
    {
        for (Measure& measure : part.measures)
        {
            for (Voice& voice : measure.voices)
            {
                for (ScoreEvent& event : voice.events)
                {
                    if (event.duration >= 0)
                    {
                        continue;
                    }

                    event.duration = 0;
                    event.isGrace = true;

                    ++repairs;
                    addRepair(
                        score, locate(part, measure, voice), "duration",
                        "negative duration clamped to zero");
                }

                const auto removed = std::remove_if(
                    voice.events.begin(), voice.events.end(),
                    [](const ScoreEvent& event) { return event.duration == 0 && !event.isGrace; });

                if (removed != voice.events.end())
                {
                    // A zero-length event that is not a grace note occupies no
                    // time, so nothing can render or play it.
                    repairs += static_cast<int>(std::distance(removed, voice.events.end()));
                    voice.events.erase(removed, voice.events.end());

                    addRepair(
                        score, locate(part, measure, voice), "note",
                        "dropped a zero-length event that was not a grace note");
                }
            }
        }
    }

    return repairs;
}

int enforceVoiceOrdering(Score& score)
{
    int repairs = 0;

    for (Part& part : score.parts)
    {
        for (Measure& measure : part.measures)
        {
            for (Voice& voice : measure.voices)
            {
                const bool wasSorted = std::is_sorted(
                    voice.events.begin(), voice.events.end(),
                    [](const ScoreEvent& lhs, const ScoreEvent& rhs)
                    { return lhs.onset < rhs.onset; });

                if (!wasSorted)
                {
                    // Stable, so a grace note keeps its position relative to
                    // the note it decorates when both share an onset.
                    std::stable_sort(
                        voice.events.begin(), voice.events.end(),
                        [](const ScoreEvent& lhs, const ScoreEvent& rhs)
                        { return lhs.onset < rhs.onset; });

                    ++repairs;
                    addRepair(
                        score, locate(part, measure, voice), "voice",
                        "events were not in ascending time order; reordered");
                }

                // Truncate anything running into the next event. A grace note
                // has no duration, so it cannot overlap and is skipped.
                for (std::size_t index = 0; index + 1 < voice.events.size(); ++index)
                {
                    ScoreEvent& current = voice.events[index];
                    const ScoreEvent& next = voice.events[index + 1];

                    if (current.isGrace || endOf(current) <= next.onset)
                    {
                        continue;
                    }

                    current.duration = std::max(Tick{0}, next.onset - current.onset);

                    ++repairs;
                    addRepair(
                        score, locate(part, measure, voice), "note",
                        "overlapping events in one voice; the earlier was shortened");
                }
            }
        }
    }

    return repairs;
}

int enforceMeasureBounds(Score& score)
{
    int repairs = 0;

    for (Part& part : score.parts)
    {
        for (Measure& measure : part.measures)
        {
            // A pickup legitimately holds less than its time signature, which
            // is the whole reason it is flagged. It is only exempt from being
            // *short*, not from overflowing.
            const Tick limit = measure.nominalDuration;

            if (limit <= 0)
            {
                continue;
            }

            for (Voice& voice : measure.voices)
            {
                const auto beyond = std::remove_if(
                    voice.events.begin(), voice.events.end(),
                    [limit](const ScoreEvent& event) { return event.onset >= limit; });

                if (beyond != voice.events.end())
                {
                    repairs += static_cast<int>(std::distance(beyond, voice.events.end()));
                    voice.events.erase(beyond, voice.events.end());

                    addRepair(
                        score, locate(part, measure, voice), "measure",
                        "dropped events starting after the end of the measure");
                }

                for (ScoreEvent& event : voice.events)
                {
                    if (endOf(event) <= limit)
                    {
                        continue;
                    }

                    event.duration = limit - event.onset;

                    ++repairs;
                    addRepair(
                        score, locate(part, measure, voice), "measure",
                        "an event ran past the end of the measure; shortened to fit");
                }
            }
        }
    }

    return repairs;
}

int enforceMeasureAlignment(Score& score)
{
    if (score.parts.empty())
    {
        return 0;
    }

    int repairs = 0;

    std::size_t longest = 0;

    for (const Part& part : score.parts)
    {
        longest = std::max(longest, part.measures.size());
    }

    // Every measure position takes the longest nominal duration any part claims
    // for it, so no part's music is truncated by another part's shorter bar.
    std::vector<Tick> nominalDurations(longest, 0);
    std::vector<std::string> printedNumbers(longest);
    std::vector<bool> pickupFlags(longest, false);

    for (const Part& part : score.parts)
    {
        for (std::size_t index = 0; index < part.measures.size(); ++index)
        {
            const Measure& measure = part.measures[index];

            nominalDurations[index] = std::max(nominalDurations[index], measure.nominalDuration);

            if (printedNumbers[index].empty())
            {
                printedNumbers[index] = measure.printedNumber;
            }

            if (measure.isPickup)
            {
                pickupFlags[index] = true;
            }
        }
    }

    for (Part& part : score.parts)
    {
        if (part.measures.size() < longest)
        {
            const std::size_t missing = longest - part.measures.size();

            for (std::size_t index = part.measures.size(); index < longest; ++index)
            {
                Measure padding;
                padding.printedNumber = printedNumbers[index];
                padding.index = index;
                padding.nominalDuration = nominalDurations[index];
                padding.isPickup = pickupFlags[index];

                Voice voice;
                voice.number = 1;
                voice.events = {wholeMeasureRest(nominalDurations[index])};
                padding.voices = {voice};

                part.measures.push_back(std::move(padding));
            }

            repairs += static_cast<int>(missing);

            DiagnosticLocation location;
            location.partId = part.id;

            addRepair(
                score, location, "part",
                "part had fewer measures than the score; padded with whole-measure rests");
        }

        // Recompute index, nominal duration, and start together so a start tick
        // can never drift out of step with the measure lengths preceding it.
        Tick start = 0;

        for (std::size_t index = 0; index < part.measures.size(); ++index)
        {
            Measure& measure = part.measures[index];

            if (measure.index != index || measure.start != start ||
                measure.nominalDuration != nominalDurations[index])
            {
                ++repairs;
            }

            measure.index = index;
            measure.start = start;
            measure.nominalDuration = nominalDurations[index];

            start += nominalDurations[index];
        }
    }

    return repairs;
}

int enforceUniquePartIds(Score& score)
{
    int repairs = 0;

    std::unordered_set<std::string> seen;

    for (std::size_t index = 0; index < score.parts.size(); ++index)
    {
        Part& part = score.parts[index];

        const bool isMissing = part.id.empty();
        const bool isDuplicate = !isMissing && seen.count(part.id) > 0;

        if (!isMissing && !isDuplicate)
        {
            seen.insert(part.id);
            continue;
        }

        // Generate something stable and obviously synthetic. #39 will name a
        // selected part by this id, so two parts sharing one is not survivable.
        std::string generated = "P" + std::to_string(index + 1);

        while (seen.count(generated) > 0)
        {
            generated += "x";
        }

        DiagnosticLocation location;
        location.partId = generated;

        addRepair(
            score, location, "score-part",
            isMissing ? "part had no identifier; generated one"
                      : "part identifier was already used by another part; generated a new one");

        part.id = generated;
        seen.insert(generated);
        ++repairs;
    }

    return repairs;
}

int enforceTieIntegrity(Score& score)
{
    int repairs = 0;

    for (Part& part : score.parts)
    {
        // Resolve a NoteRef against this part, or nullptr if it names something
        // that does not exist. Resolution goes all the way down to the note,
        // because a tie links notes rather than chords.
        const auto resolve = [&part](const NoteRef& reference) -> const Note*
        {
            if (reference.measureIndex >= part.measures.size())
            {
                return nullptr;
            }

            const Measure& measure = part.measures[reference.measureIndex];

            if (reference.voiceIndex >= measure.voices.size())
            {
                return nullptr;
            }

            const Voice& voice = measure.voices[reference.voiceIndex];

            if (reference.eventIndex >= voice.events.size())
            {
                return nullptr;
            }

            const ScoreEvent& event = voice.events[reference.eventIndex];

            if (reference.noteIndex >= event.notes.size())
            {
                return nullptr;
            }

            return &event.notes[reference.noteIndex];
        };

        for (std::size_t measureIndex = 0; measureIndex < part.measures.size(); ++measureIndex)
        {
            Measure& measure = part.measures[measureIndex];

            for (std::size_t voiceIndex = 0; voiceIndex < measure.voices.size(); ++voiceIndex)
            {
                Voice& voice = measure.voices[voiceIndex];

                for (std::size_t eventIndex = 0; eventIndex < voice.events.size(); ++eventIndex)
                {
                    ScoreEvent& event = voice.events[eventIndex];

                    for (std::size_t noteIndex = 0; noteIndex < event.notes.size(); ++noteIndex)
                    {
                        Note& note = event.notes[noteIndex];
                        const NoteRef self{measureIndex, voiceIndex, eventIndex, noteIndex};

                        const auto check = [&](std::optional<NoteRef>& link, bool isForward) -> bool
                        {
                            if (!link.has_value())
                            {
                                return false;
                            }

                            const Note* target = resolve(*link);

                            // The far end must exist, mirror the link back, and
                            // agree on pitch. A tie may legitimately be spelled
                            // differently either side of a barline -- a G-sharp
                            // tied to an A-flat -- so this compares sounding
                            // pitch, not spelling.
                            const bool mirrored =
                                target != nullptr &&
                                (isForward ? target->tiedFrom : target->tiedTo) == self;

                            const bool sameSound =
                                target != nullptr && soundsSameAs(note.pitch, target->pitch);

                            if (mirrored && sameSound)
                            {
                                return false;
                            }

                            link.reset();
                            return true;
                        };

                        if (check(note.tiedTo, true))
                        {
                            ++repairs;
                            addRepair(
                                score, locate(part, measure, voice), "tie",
                                "tie start had no matching end; dropped");
                        }

                        if (check(note.tiedFrom, false))
                        {
                            ++repairs;
                            addRepair(
                                score, locate(part, measure, voice), "tie",
                                "tie end had no matching start; dropped");
                        }
                    }
                }
            }
        }
    }

    return repairs;
}

int enforceTempoMap(Score& score)
{
    const std::vector<TempoEntry> before = score.tempoMap.entries();

    score.tempoMap = TempoMap::build(before);

    if (score.tempoMap.entries().size() == before.size())
    {
        return 0;
    }

    addRepair(
        score, DiagnosticLocation{}, "sound",
        "tempo markings were unordered, duplicated, or absent; normalised");

    return 1;
}

int enforceDiagnosticLocations(Score& score)
{
    int repairs = 0;

    std::unordered_set<std::string> partIds;
    std::unordered_set<std::string> measureNumbers;
    std::unordered_set<int> voiceNumbers;

    for (const Part& part : score.parts)
    {
        partIds.insert(part.id);

        for (const Measure& measure : part.measures)
        {
            measureNumbers.insert(measure.printedNumber);

            for (const Voice& voice : measure.voices)
            {
                voiceNumbers.insert(voice.number);
            }
        }
    }

    for (Diagnostic& diagnostic : score.diagnostics)
    {
        DiagnosticLocation& location = diagnostic.location;

        if (location.partId.has_value() && partIds.count(*location.partId) == 0)
        {
            location.partId.reset();
            ++repairs;
        }

        if (location.measureNumber.has_value() &&
            measureNumbers.count(*location.measureNumber) == 0)
        {
            location.measureNumber.reset();
            ++repairs;
        }

        if (location.voice.has_value() && voiceNumbers.count(*location.voice) == 0)
        {
            location.voice.reset();
            ++repairs;
        }
    }

    return repairs;
}

void enforceInvariants(Score& score)
{
    // Order is load-bearing -- see the header. Anything that can move or remove
    // an event must run before tie validation, which resolves NoteRefs.
    enforceConsistentPitches(score);
    enforceNonNegativeDurations(score);
    enforceVoiceOrdering(score);
    enforceMeasureBounds(score);
    enforceMeasureAlignment(score);
    enforceUniquePartIds(score);
    enforceTieIntegrity(score);
    enforceTempoMap(score);

    // Last: part ids may have been generated and measures padded above, so the
    // set of valid locations is only final now.
    enforceDiagnosticLocations(score);
}
} // namespace score
