#include "MusicXmlMeasureReader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "MusicXmlSubset.h"
#include "MusicXmlValueParsing.h"

namespace score::musicxml
{
namespace
{
// Convert a source duration into model ticks, diagnosing an inexact conversion
// rather than letting the rounding accumulate unnoticed. Rounding that is
// silently discarded drifts across a measure until a voice no longer lines up
// with its barline, which is a wrong score that never announces itself.
Tick rescaleDuration(
    const std::string& text,
    const PartCursorState& state,
    ReadContext& context,
    const DiagnosticLocation& location,
    const char* elementName)
{
    const std::optional<long long> raw = parseInteger(text);

    if (!raw.has_value())
    {
        return 0;
    }

    const RescaledDuration rescaled = rescaleToTicks(static_cast<Tick>(*raw), state.divisions);

    if (!rescaled.exact)
    {
        context.diagnostics.addRepair(
            location, elementName,
            "A duration of " + text + " in units of " + std::to_string(state.divisions) +
                " per quarter note does not convert exactly to the score's time base, so it was "
                "rounded to the nearest tick.");
    }

    return rescaled.ticks;
}

// Read a <pitch> into the model, or nothing if it is unreadable.
std::optional<Pitch>
readPitch(const XmlNode& pitchNode, ReadContext& context, const DiagnosticLocation& location)
{
    const std::optional<Step> step = parseStep(childValue(pitchNode, "step"));

    if (!step.has_value())
    {
        return std::nullopt;
    }

    const double alter = parseDecimal(childValue(pitchNode, "alter")).value_or(0.0);
    const int octave = static_cast<int>(parseInteger(childValue(pitchNode, "octave")).value_or(4));

    // <alter> is a decimal and permits microtones. They are outside the subset,
    // so they round to the nearest semitone and say so rather than being
    // truncated silently into a different note.
    if (alter != std::round(alter))
    {
        context.diagnostics.addUnsupported(
            location, "alter", "A microtonal alteration was rounded to the nearest semitone.");
    }

    return pitchFromSpelling(*step, static_cast<int>(std::lround(alter)), octave);
}

// Sounding ties only. <tie> is the sounding tie; <notations><tied> is its
// engraved counterpart and <slur> is a different thing entirely. The three look
// alike on the page and this is the distinction most often got wrong, so it is
// made in exactly one place.
void readTieFlags(const XmlNode& noteNode, PendingEvent& pending)
{
    for (const XmlNode* tie : findChildren(noteNode, "tie"))
    {
        const std::string type = attributeValue(*tie, "type");
        pending.tieStart = pending.tieStart || type == "start";
        pending.tieStop = pending.tieStop || type == "stop";
    }
}

void readLyrics(const XmlNode& noteNode, ScoreEvent& event)
{
    for (const XmlNode* lyricNode : findChildren(noteNode, "lyric"))
    {
        LyricSyllable syllable;
        syllable.verse = intAttribute(*lyricNode, "number").value_or(1);
        syllable.position = parseSyllabic(childValue(*lyricNode, "syllabic"));
        syllable.text = childValue(*lyricNode, "text");
        syllable.extend = findChild(*lyricNode, "extend") != nullptr;

        // A syllable with neither text nor an extender says nothing. An
        // extender alone is a melisma continuing the previous syllable and is
        // worth keeping.
        if (!syllable.text.empty() || syllable.extend)
        {
            event.lyrics.push_back(std::move(syllable));
        }
    }
}

PendingVoice& voiceFor(std::vector<PendingVoice>& voices, int number)
{
    const auto found = std::find_if(
        voices.begin(), voices.end(),
        [number](const PendingVoice& voice) { return voice.number == number; });

    if (found != voices.end())
    {
        return *found;
    }

    return voices.emplace_back(PendingVoice{number, {}});
}

void readAttributes(
    const XmlNode& attributesNode,
    ReadContext& context,
    Part& part,
    Measure& measure,
    PartCursorState& state,
    std::size_t measureIndex)
{
    if (const XmlNode* divisions = findChild(attributesNode, "divisions"); divisions != nullptr)
    {
        const std::optional<long long> value = parseInteger(divisions->value);

        if (value.has_value() && *value > 0)
        {
            const Tick declared = static_cast<Tick>(*value);

            if (!state.divisionsDeclared || declared != state.divisions)
            {
                part.sourceDivisions.push_back({measureIndex, static_cast<int>(declared)});
            }

            state.divisions = declared;
            state.divisionsDeclared = true;
        }
        else
        {
            context.diagnostics.addRepair(
                locationOf(part, measure), "divisions",
                "The part declares a non-positive number of divisions per quarter note, which no "
                "duration can be measured against; the previous value was kept.");
        }
    }

    if (const XmlNode* key = findChild(attributesNode, "key"); key != nullptr)
    {
        KeySignature signature;
        signature.fifths = static_cast<int>(parseInteger(childValue(*key, "fifths")).value_or(0));
        signature.mode = childValue(*key, "mode");
        measure.attributes.key = signature;
    }

    if (const XmlNode* time = findChild(attributesNode, "time"); time != nullptr)
    {
        const std::optional<long long> beats = parseInteger(childValue(*time, "beats"));
        const std::optional<long long> beatType = parseInteger(childValue(*time, "beat-type"));

        if (beats.has_value() && beatType.has_value() && *beats > 0 && *beatType > 0)
        {
            TimeSignature signature;
            signature.beats = static_cast<int>(*beats);
            signature.beatType = static_cast<int>(*beatType);
            measure.attributes.time = signature;
            state.time = signature;
        }
        else
        {
            context.diagnostics.addRepair(
                locationOf(part, measure), "time",
                "The time signature could not be read, so the previous one was kept.");
        }
    }

    if (const std::optional<long long> staves = parseInteger(childValue(attributesNode, "staves"));
        staves.has_value() && *staves > 0)
    {
        state.staffCount = static_cast<int>(*staves);
        part.staffCount = std::max(part.staffCount, state.staffCount);
    }

    for (const XmlNode* clefNode : findChildren(attributesNode, "clef"))
    {
        Clef clef;
        clef.sign = childValue(*clefNode, "sign");
        clef.line = static_cast<int>(parseInteger(childValue(*clefNode, "line")).value_or(2));
        clef.octaveChange =
            static_cast<int>(parseInteger(childValue(*clefNode, "clef-octave-change")).value_or(0));
        clef.staff = intAttribute(*clefNode, "number").value_or(1);

        if (clef.sign.empty())
        {
            clef.sign = "G";
        }

        measure.attributes.clefs.push_back(clef);
    }

    if (findChild(attributesNode, "transpose") != nullptr)
    {
        context.diagnostics.addUnsupported(
            locationOf(part, measure), "transpose", unsupportedConstructs().at("transpose"));
    }
}

void readNote(
    const XmlNode& noteNode,
    ReadContext& context,
    const Part& part,
    const Measure& measure,
    const PartCursorState& state,
    std::vector<PendingVoice>& voices,
    Tick& cursor,
    int& lastVoiceNumber)
{
    const bool isChordTone = findChild(noteNode, "chord") != nullptr;
    const bool isGrace = findChild(noteNode, "grace") != nullptr;
    const XmlNode* restNode = findChild(noteNode, "rest");
    const XmlNode* pitchNode = findChild(noteNode, "pitch");

    // A note without <voice> belongs to voice 1, except in a chord, where it
    // belongs to whatever the note it is stacked on belongs to.
    const std::optional<long long> declaredVoice = parseInteger(childValue(noteNode, "voice"));
    const int voiceNumber = declaredVoice.has_value() ? static_cast<int>(*declaredVoice)
                                                      : (isChordTone ? lastVoiceNumber : 1);
    lastVoiceNumber = voiceNumber;

    PendingVoice& voice = voiceFor(voices, voiceNumber);
    const DiagnosticLocation location = locationOf(part, measure, voiceNumber);

    if (findChild(noteNode, "unpitched") != nullptr)
    {
        context.diagnostics.addUnsupported(
            location, "unpitched", unsupportedConstructs().at("unpitched"));
    }

    // A chord tone joins the event already on the cursor rather than starting a
    // new one, and consumes no additional time. Invariant 2 depends on it: "no
    // two events in a voice overlap" only means anything if a chord is one
    // event rather than several simultaneous ones.
    if (isChordTone && !voice.events.empty() && pitchNode != nullptr)
    {
        PendingEvent& target = voice.events.back();

        if (const std::optional<Pitch> pitch = readPitch(*pitchNode, context, location);
            pitch.has_value())
        {
            target.event.pitches.push_back(*pitch);
            target.event.kind = EventKind::chord;
            readTieFlags(noteNode, target);
        }

        ++context.eventCount;

        return;
    }

    PendingEvent pending;
    pending.event.onset = cursor;
    pending.event.isGrace = isGrace;
    pending.event.staff = static_cast<int>(parseInteger(childValue(noteNode, "staff")).value_or(1));

    if (restNode != nullptr)
    {
        pending.event.kind = EventKind::rest;

        // <rest measure="yes"> is a whole-measure rest: it lasts the bar,
        // whatever the bar is, and exporters routinely omit its <duration>.
        if (attributeValue(*restNode, "measure") == "yes")
        {
            pending.event.duration = measure.nominalDuration;
        }
    }
    else if (pitchNode != nullptr)
    {
        if (const std::optional<Pitch> pitch = readPitch(*pitchNode, context, location);
            pitch.has_value())
        {
            pending.event.kind = EventKind::note;
            pending.event.pitches.push_back(*pitch);
        }
        else
        {
            context.diagnostics.addRepair(
                location, "step", "A note has no readable pitch, so it was imported as a rest.");
            pending.event.kind = EventKind::rest;
        }
    }
    else
    {
        pending.event.kind = EventKind::rest;
    }

    // A grace note takes its time from performance rather than from the bar's
    // arithmetic, so it has no duration and does not move the cursor.
    if (isGrace)
    {
        pending.event.duration = 0;
    }
    else if (pending.event.duration == 0)
    {
        pending.event.duration =
            rescaleDuration(childValue(noteNode, "duration"), state, context, location, "duration");
    }

    if (const XmlNode* modification = findChild(noteNode, "time-modification");
        modification != nullptr)
    {
        const std::optional<long long> actual =
            parseInteger(childValue(*modification, "actual-notes"));
        const std::optional<long long> normal =
            parseInteger(childValue(*modification, "normal-notes"));

        if (actual.has_value() && normal.has_value() && *actual > 0 && *normal > 0)
        {
            pending.event.tuplet = {static_cast<int>(*actual), static_cast<int>(*normal)};
        }
    }

    readTieFlags(noteNode, pending);
    readLyrics(noteNode, pending.event);

    if (const XmlNode* notations = findChild(noteNode, "notations"); notations != nullptr)
    {
        for (const auto& [name, message] : unsupportedConstructs())
        {
            if (findChild(*notations, name) != nullptr)
            {
                context.diagnostics.addUnsupported(location, name, message);
            }
        }
    }

    if (!isGrace)
    {
        cursor += pending.event.duration;
    }

    voice.events.push_back(std::move(pending));
    ++context.eventCount;
}

void readDirection(
    const XmlNode& directionNode,
    ReadContext& context,
    const Part& part,
    Measure& measure,
    const PartCursorState& state,
    Tick cursor,
    Tick measureStart)
{
    Tick onset = cursor;

    if (const XmlNode* offset = findChild(directionNode, "offset"); offset != nullptr)
    {
        if (const std::optional<long long> raw = parseInteger(offset->value); raw.has_value())
        {
            onset += rescaleToTicks(static_cast<Tick>(*raw), state.divisions).ticks;
        }
    }

    onset = std::max(Tick{0}, onset);

    const int staff =
        static_cast<int>(parseInteger(childValue(directionNode, "staff")).value_or(1));

    std::optional<double> metronomeTempo;
    std::string tempoText;

    for (const XmlNode* directionType : findChildren(directionNode, "direction-type"))
    {
        for (const XmlNode& child : directionType->children)
        {
            if (child.name == "dynamics")
            {
                for (const XmlNode& marking : child.children)
                {
                    if (!isDynamicMarking(marking.name))
                    {
                        continue;
                    }

                    Direction direction;
                    direction.kind = DirectionKind::dynamic;
                    direction.onset = onset;
                    direction.staff = staff;
                    direction.text = marking.name;
                    measure.directions.push_back(std::move(direction));
                }
            }
            else if (child.name == "words" && tempoText.empty())
            {
                tempoText = child.value;
            }
            else if (child.name == "metronome")
            {
                const std::string beatUnit = childValue(child, "beat-unit");
                const int dots = static_cast<int>(findChildren(child, "beat-unit-dot").size());
                const std::optional<double> perMinute =
                    parseDecimal(childValue(child, "per-minute"));

                if (perMinute.has_value() && *perMinute > 0.0)
                {
                    metronomeTempo = *perMinute * quarterNotesPerBeatUnit(beatUnit, dots);
                }
            }
            else if (unsupportedConstructs().count(child.name) > 0)
            {
                context.diagnostics.addUnsupported(
                    locationOf(part, measure), child.name, unsupportedConstructs().at(child.name));
            }
        }
    }

    std::optional<double> soundTempo;

    if (const XmlNode* sound = findChild(directionNode, "sound"); sound != nullptr)
    {
        if (const std::optional<double> tempo = parseDecimal(attributeValue(*sound, "tempo"));
            tempo.has_value() && *tempo > 0.0)
        {
            soundTempo = *tempo;
        }
    }

    // Tempo arrives two ways and the two can disagree. <sound tempo="..."> is
    // the explicit playback value in quarter notes per minute; <metronome> is
    // the marking engraved on the page, which an exporter may leave stale or
    // set for appearance. So `sound` wins, and the metronome marking is used
    // only when there is no `sound`. The <words> text is kept either way,
    // because "Allegro" is what a musician actually reads.
    if (soundTempo.has_value() && metronomeTempo.has_value() &&
        std::abs(*soundTempo - *metronomeTempo) > 0.5)
    {
        context.diagnostics.addRepair(
            locationOf(part, measure), "metronome",
            "The engraved metronome mark and the playback tempo disagree (" +
                std::to_string(static_cast<int>(*metronomeTempo)) + " against " +
                std::to_string(static_cast<int>(*soundTempo)) +
                " quarter notes per minute); the playback tempo was used.");
    }

    const std::optional<double> tempo = soundTempo.has_value() ? soundTempo : metronomeTempo;

    if (!tempo.has_value())
    {
        return;
    }

    Direction direction;
    direction.kind = DirectionKind::tempo;
    direction.onset = onset;
    direction.staff = staff;
    direction.text = tempoText;
    direction.beatsPerMinute = *tempo;
    measure.directions.push_back(std::move(direction));

    context.tempoEntries.push_back({measureStart + onset, *tempo});
}

void readBarline(const XmlNode& barlineNode, Measure& measure)
{
    if (const XmlNode* repeat = findChild(barlineNode, "repeat"); repeat != nullptr)
    {
        // "forward" is the |: that opens a repeated section, "backward" the :|
        // that closes it. Captured, never interpreted -- decision 3.
        const std::string direction = attributeValue(*repeat, "direction");
        measure.repeats.repeatStart = measure.repeats.repeatStart || direction == "forward";
        measure.repeats.repeatEnd = measure.repeats.repeatEnd || direction == "backward";

        if (const std::optional<int> times = intAttribute(*repeat, "times"); times.has_value())
        {
            measure.repeats.repeatTimes = *times;
        }
    }

    const XmlNode* ending = findChild(barlineNode, "ending");

    if (ending == nullptr)
    {
        return;
    }

    // MusicXML writes ending numbers as a comma-separated list: "1, 2".
    const std::string numbers = attributeValue(*ending, "number");
    std::string current;

    for (const char character : numbers + ",")
    {
        if (character != ',')
        {
            if (std::isspace(static_cast<unsigned char>(character)) == 0)
            {
                current.push_back(character);
            }

            continue;
        }

        const std::optional<long long> value = parseInteger(current);
        current.clear();

        if (!value.has_value())
        {
            continue;
        }

        const int number = static_cast<int>(*value);
        auto& seen = measure.repeats.endingNumbers;

        if (std::find(seen.begin(), seen.end(), number) == seen.end())
        {
            seen.push_back(number);
        }
    }
}

// Freeze the pending voices into the measure.
//
// Sorted here rather than left to the invariant pass, because tie resolution
// needs each voice's final order to build an EventRef against, and the
// invariants run after the importer is finished. Voice order is by voice number
// so that a voice keeps the same index in every measure it appears in, which is
// what makes an EventRef stable across a barline.
void freezeVoices(std::vector<PendingVoice>& pendingVoices, Measure& measure)
{
    for (PendingVoice& voice : pendingVoices)
    {
        std::stable_sort(
            voice.events.begin(), voice.events.end(),
            [](const PendingEvent& lhs, const PendingEvent& rhs)
            { return lhs.event.onset < rhs.event.onset; });
    }

    std::stable_sort(
        pendingVoices.begin(), pendingVoices.end(),
        [](const PendingVoice& lhs, const PendingVoice& rhs) { return lhs.number < rhs.number; });

    measure.voices.reserve(pendingVoices.size());

    for (const PendingVoice& pending : pendingVoices)
    {
        Voice voice;
        voice.number = pending.number;
        voice.events.reserve(pending.events.size());

        for (const PendingEvent& event : pending.events)
        {
            voice.events.push_back(event.event);
        }

        measure.voices.push_back(std::move(voice));
    }
}
} // namespace

void readMeasure(
    const XmlNode& measureNode,
    ReadContext& context,
    Part& part,
    PartCursorState& state,
    std::size_t measureIndex,
    Tick measureStart,
    std::vector<PendingVoice>& pendingVoices)
{
    Measure measure;
    measure.index = measureIndex;
    measure.start = measureStart;
    measure.printedNumber = attributeValue(measureNode, "number");
    measure.isPickup = attributeValue(measureNode, "implicit") == "yes";
    measure.nominalDuration = nominalTicks(state.time);

    if (measure.printedNumber.empty())
    {
        // Not a repair: the model's contract is that this is what the file
        // printed, and "nothing" is a legitimate answer for an exporter to
        // give. The index still identifies the measure; this only gives a
        // diagnostic something to quote.
        measure.printedNumber = std::to_string(measureIndex + 1);
    }

    Tick cursor = 0;
    int lastVoiceNumber = 1;

    for (const XmlNode& child : measureNode.children)
    {
        if (child.name == "attributes")
        {
            readAttributes(child, context, part, measure, state, measureIndex);

            // A time signature declared here changes what this measure should
            // hold, so the nominal duration is recomputed after reading it.
            measure.nominalDuration = nominalTicks(state.time);
        }
        else if (child.name == "note")
        {
            readNote(child, context, part, measure, state, pendingVoices, cursor, lastVoiceNumber);
        }
        else if (child.name == "backup")
        {
            cursor -= rescaleDuration(
                childValue(child, "duration"), state, context, locationOf(part, measure), "backup");

            if (cursor < 0)
            {
                context.diagnostics.addRepair(
                    locationOf(part, measure), "backup",
                    "A backup moved the writing position before the start of the measure; it was "
                    "clamped to the start.");
                cursor = 0;
            }
        }
        else if (child.name == "forward")
        {
            cursor += rescaleDuration(
                childValue(child, "duration"), state, context, locationOf(part, measure),
                "forward");
        }
        else if (child.name == "direction")
        {
            readDirection(child, context, part, measure, state, cursor, measureStart);
        }
        else if (child.name == "barline")
        {
            readBarline(child, measure);
        }
        else if (child.name == "sound")
        {
            if (const std::optional<double> tempo = parseDecimal(attributeValue(child, "tempo"));
                tempo.has_value() && *tempo > 0.0)
            {
                context.tempoEntries.push_back({measureStart + cursor, *tempo});
            }
        }
        else if (unsupportedConstructs().count(child.name) > 0)
        {
            context.diagnostics.addUnsupported(
                locationOf(part, measure), child.name, unsupportedConstructs().at(child.name));
        }
    }

    // A score that never declares a time signature still needs a bound for
    // invariant 7 to check each voice against, and 4/4 is what every notation
    // program assumes in its absence.
    if (measure.nominalDuration <= 0)
    {
        measure.nominalDuration = nominalTicks(TimeSignature{});
    }

    freezeVoices(pendingVoices, measure);
    part.measures.push_back(std::move(measure));
}
} // namespace score::musicxml
