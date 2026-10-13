#include "ScoreLayout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "Accidentals.h"
#include "Beams.h"
#include "NoteValue.h"
#include "Stems.h"
#include "SystemHead.h"

#include "platform/score/ScoreTraversal.h"

namespace score::engraving
{
namespace
{
// Padding either side of a glyph group. The room at the head of a system is
// not a constant -- it is whatever that system's clefs, key and time signature
// actually need, which is `SystemHead.h`.
constexpr double glyphPadding = 0.6;
constexpr double barlinePadding = 0.8;
constexpr double staffHeight = 4.0;

// Gap between a notehead and its first augmentation dot, and between dots.
constexpr double dotGap = 0.35;

// How far apart two voices' rests move on a shared staff, in staff steps.
constexpr int restVoiceOffsetSteps = 2;

// How steeply width grows with duration. 1.0 would be strict proportionality,
// which is the thing to avoid; 0.6 is the usual neighbourhood for a power-law
// spacing rule.
constexpr double spacingExponent = 0.6;

// A whole rest hangs below the fourth line and a half rest sits on the third;
// everything shorter is centred. Engraving convention, not arithmetic, so it is
// a table rather than a formula.
[[nodiscard]] int restStepFor(Glyph rest) noexcept
{
    if (rest == Glyph::restWhole)
    {
        return 6;
    }

    return 4;
}

// One note, already placed vertically and already told whether it needs an
// accidental.
struct PreparedNote
{
    Glyph notehead = Glyph::noteheadBlack;
    int staffStep = 0;
    std::optional<Glyph> accidental;
};

// One event, with everything that does not depend on the available width
// already decided.
//
// Prepared in a first pass because accidentals are a running state -- what is
// in force depends on every earlier note in the bar -- and running that state
// while also deciding column widths would mean doing it once per candidate
// system break.
struct PreparedEvent
{
    std::size_t partIndex = 0;
    std::size_t measureIndex = 0;
    int staff = 1;

    Tick absoluteOnset = 0;

    bool isRest = false;

    // For a rest: the one glyph. For a note or chord: one entry per note.
    Glyph restGlyph = Glyph::restQuarter;
    int restStaffStep = 4;
    std::vector<PreparedNote> notes;

    // What this duration is *written* as: notehead, flags, dots. The model
    // carries no notated type, so this is derived, once, here.
    NoteValue value;

    // Whether a stem is drawn at all, and which way. Decided in this pass
    // rather than at placement because the first rule needs the voice, and the
    // voice is only in hand here.
    bool hasStem = false;
    bool stemUp = true;

    // The outermost noteheads of the chord, which are what a stem attaches to.
    // Equal for a single note; meaningless for a rest.
    int lowestStep = 0;
    int highestStep = 0;

    // Which voice wrote it. Beams never join two voices, and stem direction
    // already depends on it.
    int voiceNumber = 1;

    // Onset within its own measure, which is what beat grouping is measured
    // against. The absolute onset would need the measure start subtracting
    // back off at every use.
    Tick onsetInMeasure = 0;

    // True once a beam has claimed it, which is what suppresses its flag: a
    // beamed note with a flag as well would read as twice as short.
    bool beamed = false;

    // Horizontal room this event's glyphs need, accidentals included.
    double width = 0.0;
};

// One beam: the prepared events it joins, in order.
//
// Indices into the part's prepared list rather than pointers, because the list
// is still being appended to while these are collected and a pointer into a
// growing vector is a dangling pointer waiting for a reallocation.
struct BeamGroup
{
    std::vector<std::size_t> events;
};

// What is in force at the start of a measure, per staff.
struct StaffState
{
    Clef clef;
    AccidentalState accidentals;
};

// What one part's head shows at the start of each of its measures, which is
// what a system starting at that measure must draw.
//
// Recorded per measure rather than per system because system breaking has not
// happened yet when this is built, and because the head's width is an input to
// that decision -- a bar cannot be measured against the available width until
// the room ahead of it is known.
struct PartAttributeTrack
{
    // One entry per measure, each holding one clef per staff of the part.
    std::vector<std::vector<Clef>> clefs;

    // Key and time signature are per part, not per staff (ScoreAttributes.h),
    // and are drawn on every staff of it.
    std::vector<std::optional<KeySignature>> keys;
    std::vector<std::optional<TimeSignature>> times;
};

// Room an event's dots take, after the notehead.
[[nodiscard]] double dotsWidth(int dots)
{
    return dots <= 0 ? 0.0 : dots * (dotGap + advanceWidth(Glyph::augmentationDot));
}

[[nodiscard]] double widthOf(const PreparedEvent& event)
{
    if (event.isRest)
    {
        return advanceWidth(event.restGlyph) + dotsWidth(event.value.dots);
    }

    double widest = 0.0;

    for (const PreparedNote& note : event.notes)
    {
        double width = advanceWidth(note.notehead);

        if (note.accidental.has_value())
        {
            width += advanceWidth(*note.accidental) + glyphPadding;
        }

        widest = std::max(widest, width);
    }

    // A flag hangs to the right of an up stem and overhangs the notehead. Not
    // counting it is how a flag ends up drawn across the next column. A beamed
    // event has no flag and so needs none of this.
    if (event.hasStem && !event.beamed && event.value.flags > 0 && event.stemUp)
    {
        widest += advanceWidth(flagGlyphFor(event.value.flags, true));
    }

    return widest + dotsWidth(event.value.dots);
}

// The clef governing a staff after applying a measure's changes.
void applyClefChanges(const AttributeChanges& changes, std::vector<StaffState>& staves)
{
    for (const Clef& clef : changes.clefs)
    {
        // MusicXML numbers staves from 1, and a part that declares one staff
        // may omit the number entirely. A clef naming a staff the part does not
        // have is ignored rather than resized into existence.
        const std::size_t index = static_cast<std::size_t>(std::max(1, clef.staff) - 1);

        if (index < staves.size())
        {
            staves[index].clef = clef;
        }
    }
}

// Beam the events of one measure, already prepared.
//
// Runs over each (staff, voice) separately, because a beam never joins two
// voices and never crosses the brace. Within one of those, a run of beamable
// events that share a beat group becomes a beam; anything else -- a rest, a
// quarter note, a group boundary -- ends the run. A run of one keeps its flag,
// which is what "two or more notes make a beam" means in practice.
void collectBeamGroups(
    std::vector<PreparedEvent>& prepared,
    std::size_t measureFirst,
    const TimeSignature& time,
    std::vector<BeamGroup>& groups)
{
    const Tick groupTicks = beamGroupTicks(time);

    if (groupTicks <= 0)
    {
        return;
    }

    // Keyed by staff and voice, in that order, so one part's two hands never
    // beam to each other.
    std::map<std::pair<int, int>, std::vector<std::size_t>> byVoice;

    for (std::size_t index = measureFirst; index < prepared.size(); ++index)
    {
        const PreparedEvent& event = prepared[index];

        byVoice[{event.staff, event.voiceNumber}].push_back(index);
    }

    for (auto& [key, indices] : byVoice)
    {
        std::sort(
            indices.begin(), indices.end(), [&prepared](std::size_t lhs, std::size_t rhs)
            { return prepared[lhs].onsetInMeasure < prepared[rhs].onsetInMeasure; });

        std::vector<std::size_t> run;
        Tick runGroup = 0;

        const auto flush = [&prepared, &groups, &run]()
        {
            if (run.size() >= 2)
            {
                for (const std::size_t index : run)
                {
                    prepared[index].beamed = true;
                }

                groups.push_back(BeamGroup{run});
            }

            run.clear();
        };

        for (const std::size_t index : indices)
        {
            const PreparedEvent& event = prepared[index];

            const bool beamable = !event.isRest && event.hasStem && event.value.flags > 0;

            if (!beamable)
            {
                flush();

                continue;
            }

            const Tick group = event.onsetInMeasure / groupTicks;

            if (!run.empty() && group != runGroup)
            {
                flush();
            }

            runGroup = group;
            run.push_back(index);
        }

        flush();
    }
}

// Prepare one part: walk it once, in musical order, resolving clefs, keys and
// accidentals as it goes.
//
// This is where cross-staff content is noticed, because noticing it needs the
// same walk: a voice whose events change staff within one measure is what
// MusicXML writes a cross-staff beam as.
std::vector<PreparedEvent> prepare(
    const Part& part,
    std::size_t partIndex,
    PartAttributeTrack& attributes,
    std::vector<BeamGroup>& beamGroups,
    std::vector<Diagnostic>& diagnostics)
{
    const std::size_t staffCount = static_cast<std::size_t>(std::max(1, part.staffCount));

    std::vector<StaffState> staves(staffCount);

    // A part that never declares a clef reads as treble, which is what every
    // exporter means by omitting one.
    for (std::size_t index = 0; index < staffCount; ++index)
    {
        staves[index].clef = Clef{"G", 2, 0, static_cast<int>(index) + 1};
    }

    // A two-staff part with no clef declaration is a piano: treble over bass.
    if (staffCount > 1)
    {
        staves[1].clef = Clef{"F", 4, 0, 2};
    }

    std::vector<PreparedEvent> prepared;

    attributes.clefs.assign(part.measures.size(), {});
    attributes.keys.assign(part.measures.size(), std::nullopt);
    attributes.times.assign(part.measures.size(), std::nullopt);

    std::optional<KeySignature> key;
    std::optional<TimeSignature> time;

    for (std::size_t measureIndex = 0; measureIndex < part.measures.size(); ++measureIndex)
    {
        const Measure& measure = part.measures[measureIndex];

        applyClefChanges(measure.attributes, staves);

        if (measure.attributes.key.has_value())
        {
            key = measure.attributes.key;

            for (StaffState& staff : staves)
            {
                staff.accidentals.setKey(*measure.attributes.key);
            }
        }

        if (measure.attributes.time.has_value())
        {
            time = measure.attributes.time;
        }

        for (StaffState& staff : staves)
        {
            staff.accidentals.beginMeasure();
        }

        attributes.clefs[measureIndex].clear();

        for (const StaffState& staff : staves)
        {
            attributes.clefs[measureIndex].push_back(staff.clef);
        }

        attributes.keys[measureIndex] = key;
        attributes.times[measureIndex] = time;

        // Which staff each voice has been seen on in this measure, so that a
        // voice moving between them is reported once rather than per note.
        std::map<int, int> staffOfVoice;
        bool reportedCrossStaff = false;

        // Which voices each staff carries in this measure. Built before the
        // walk rather than during it because stem direction for the *first*
        // event already depends on whether a second voice turns up later in
        // the bar, and a rule that changed halfway through a measure would be
        // worse than either answer.
        std::map<int, std::set<int>> voicesOnStaff;

        for (const Voice& voice : measure.voices)
        {
            for (const ScoreEvent& event : voice.events)
            {
                if (isPitched(event))
                {
                    voicesOnStaff[std::max(1, event.staff)].insert(voice.number);
                }
            }
        }

        const std::size_t measureFirst = prepared.size();

        for (const MeasureEventIndex& position : measureEventOrder(measure))
        {
            const Voice& voice = measure.voices[position.voiceIndex];
            const ScoreEvent& event = voice.events[position.eventIndex];

            const auto seen = staffOfVoice.find(voice.number);

            if (seen == staffOfVoice.end())
            {
                staffOfVoice.emplace(voice.number, event.staff);
            }
            else if (seen->second != event.staff && !reportedCrossStaff)
            {
                reportedCrossStaff = true;

                Diagnostic diagnostic;
                diagnostic.severity = DiagnosticSeverity::unsupported;
                diagnostic.location.partId = part.id;
                diagnostic.location.measureNumber = measure.printedNumber;
                diagnostic.location.voice = voice.number;
                diagnostic.elementName = "beam";
                diagnostic.message =
                    "This bar writes a voice across both staves. Each note is drawn on the "
                    "staff the file assigned it, but the beam joining them is not drawn.";

                diagnostics.push_back(diagnostic);
            }

            const std::size_t staffIndex =
                static_cast<std::size_t>(std::max(1, event.staff) - 1) < staves.size()
                    ? static_cast<std::size_t>(std::max(1, event.staff) - 1)
                    : 0;

            StaffState& staff = staves[staffIndex];

            PreparedEvent entry;
            entry.partIndex = partIndex;
            entry.measureIndex = measureIndex;
            entry.staff = static_cast<int>(staffIndex) + 1;
            entry.absoluteOnset = absoluteOnset(measure, event);
            entry.voiceNumber = voice.number;
            entry.onsetInMeasure = event.onset;

            // The written value, once, for notehead, flags and dots alike.
            // A rest with no duration of its own fills its bar, which is what
            // an exporter means by omitting one.
            const Tick writtenDuration =
                (!isPitched(event) && event.duration == 0)
                    ? measure.nominalDuration
                    : event.duration;

            entry.value = noteValueFor(writtenDuration, event.tuplet);

            // Nothing shorter than a sixteenth has a flag glyph in the
            // metrics table, so a thirty-second is drawn with a sixteenth's
            // flag. That is a smaller lie than the one above and still a lie,
            // so it is still said.
            if (entry.value.flags > 2)
            {
                Diagnostic diagnostic;
                diagnostic.severity = DiagnosticSeverity::unsupported;
                diagnostic.location.partId = part.id;
                diagnostic.location.measureNumber = measure.printedNumber;
                diagnostic.location.voice = voice.number;
                diagnostic.elementName = "beam";
                diagnostic.message =
                    "This note is shorter than a sixteenth. It is drawn with a sixteenth's "
                    "flag, so it reads longer than it lasts.";

                diagnostics.push_back(diagnostic);
            }

            if (!entry.value.exact)
            {
                Diagnostic diagnostic;
                diagnostic.severity = DiagnosticSeverity::unsupported;
                diagnostic.location.partId = part.id;
                diagnostic.location.measureNumber = measure.printedNumber;
                diagnostic.location.voice = voice.number;
                diagnostic.elementName = "note";
                diagnostic.message =
                    "This note's length is not one a notehead and dots can write. It is "
                    "drawn as the longest value that fits, so it reads shorter than it "
                    "lasts.";

                diagnostics.push_back(diagnostic);
            }

            if (!isPitched(event))
            {
                entry.isRest = true;
                entry.restGlyph = entry.value.rest;
                entry.restStaffStep = restStepFor(entry.restGlyph);

                // On a staff carrying two voices, their rests would otherwise
                // land on the same spot and read as one. They move apart the
                // same way their stems do: the upper voice up, the lower down.
                const auto sharing = voicesOnStaff.find(entry.staff);

                if (sharing != voicesOnStaff.end() && sharing->second.size() > 1)
                {
                    entry.restStaffStep += voice.number == *sharing->second.begin()
                                               ? restVoiceOffsetSteps
                                               : -restVoiceOffsetSteps;
                }
            }
            else
            {
                // Chord notes are in source order, not pitch order
                // (ScoreEvent.h), so stacking them means sorting here. A
                // renderer that trusted the order would draw most files
                // correctly and the rest wrongly.
                std::vector<Note> ordered = event.notes;

                std::sort(
                    ordered.begin(), ordered.end(), [](const Note& lhs, const Note& rhs)
                    { return lhs.written.midiNoteNumber < rhs.written.midiNoteNumber; });

                for (const Note& note : ordered)
                {
                    PreparedNote placed;
                    placed.notehead = entry.value.notehead;
                    placed.staffStep = staffStepFor(note.written, staff.clef);
                    placed.accidental = staff.accidentals.accidentalFor(note.written);

                    entry.notes.push_back(placed);
                }

                entry.lowestStep = entry.notes.front().staffStep;
                entry.highestStep = entry.notes.back().staffStep;

                // Everything shorter than a whole note has a stem. A whole note
                // has none, which is the one case where its absence means
                // something.
                entry.hasStem = entry.value.notehead != Glyph::noteheadWhole;

                // Direction, in the order decision 2 of design.md sets out.
                // The voice rule comes first and beats the others outright: two
                // voices on a staff with direction chosen note by note is
                // unreadable, because stem direction is the only thing telling
                // them apart.
                const auto onThisStaff = voicesOnStaff.find(entry.staff);

                if (onThisStaff != voicesOnStaff.end() && onThisStaff->second.size() > 1)
                {
                    entry.stemUp = voice.number == *onThisStaff->second.begin();
                }
                else
                {
                    entry.stemUp = stemPointsUp(entry.lowestStep, entry.highestStep);
                }
            }

            prepared.push_back(entry);
        }

        // After the walk, not during it: whether an event is beamed depends on
        // the events after it, and its width depends on whether it is beamed --
        // a beamed note has no flag, and a flag is the widest thing an event
        // carries.
        collectBeamGroups(prepared, measureFirst, time.value_or(TimeSignature{}), beamGroups);

        for (std::size_t index = measureFirst; index < prepared.size(); ++index)
        {
            prepared[index].width = widthOf(prepared[index]);
        }
    }

    return prepared;
}

// Augmentation dots after a notehead or rest.
//
// A dot never sits on a staff line: on a line it moves up into the space above,
// which is convention rather than arithmetic and is the only thing here worth
// stating.
void placeDots(StaffLayout& staff, double afterX, int staffStep, int dots)
{
    if (dots <= 0)
    {
        return;
    }

    const int dotStep = isOnLine(staffStep) ? staffStep + 1 : staffStep;

    double x = afterX + dotGap;

    for (int index = 0; index < dots; ++index)
    {
        staff.glyphs.push_back(PlacedGlyph{Glyph::augmentationDot, x, dotStep});

        x += advanceWidth(Glyph::augmentationDot) + dotGap;
    }
}

// Where one event's stem ended up, so a beam can find it again.
//
// A beam has to *move* the stems it joins, and it is placed after they exist,
// so it needs an index back into the staff that holds them.
struct PlacedStemRef
{
    bool placed = false;
    std::size_t staffIndex = 0;
    std::size_t stemIndex = 0;

    double x = 0.0;

    // Where the stem would have ended on its own. A beam may lengthen a stem
    // and must never shorten one below this.
    double idealEnd = 0.0;
};

// Place one beam group: the beam itself, and the stems it moves.
void placeBeam(
    const std::vector<PreparedEvent>& prepared,
    const std::vector<std::size_t>& events,
    const std::vector<PlacedStemRef>& stems,
    PartLayout& part)
{
    if (events.size() < 2)
    {
        return;
    }

    // Every event of the group must have been placed in this system. A group
    // split by a system break is not drawn as two half beams -- the break
    // happens at a barline and a beam never crosses one, so this is a guard
    // rather than a case.
    for (const std::size_t index : events)
    {
        if (index >= stems.size() || !stems[index].placed)
        {
            return;
        }
    }

    const std::size_t staffIndex = stems[events.front()].staffIndex;

    if (staffIndex >= part.staves.size())
    {
        return;
    }

    StaffLayout& staff = part.staves[staffIndex];

    // One direction for the whole group, from its outermost note. Half the
    // group stemming one way and half the other is not a beam.
    int lowest = prepared[events.front()].lowestStep;
    int highest = prepared[events.front()].highestStep;

    for (const std::size_t index : events)
    {
        lowest = std::min(lowest, prepared[index].lowestStep);
        highest = std::max(highest, prepared[index].highestStep);
    }

    const bool up = stemPointsUp(lowest, highest);

    // The stem ends each event would have had alone, recomputed for the
    // group's direction rather than its own: an event that would have stemmed
    // the other way has a different end entirely.
    std::vector<std::pair<double, double>> required;
    required.reserve(events.size());

    for (const std::size_t index : events)
    {
        const PreparedEvent& event = prepared[index];
        const PlacedStemRef& stem = stems[index];

        const double end =
            up ? std::max(
                     static_cast<double>(event.lowestStep + defaultStemSteps),
                     static_cast<double>(event.highestStep + chordOvershootSteps))
               : std::min(
                     static_cast<double>(event.highestStep - defaultStemSteps),
                     static_cast<double>(event.lowestStep - chordOvershootSteps));

        required.emplace_back(stem.x, end);
    }

    const BeamLine line = shiftedToClear(
        clampedBeamLine(
            required.front().first, required.front().second, required.back().first,
            required.back().second),
        required, up);

    // Every stem now ends on the beam at its own x, which is the property the
    // whole of this is for.
    for (std::size_t position = 0; position < events.size(); ++position)
    {
        const PlacedStemRef& stem = stems[events[position]];

        // A stem whose event stems the other way on its own still has to take
        // the group's direction, or the beam would join two ends that are not
        // both there.
        const PreparedEvent& event = prepared[events[position]];

        staff.stems[stem.stemIndex].fromStep = up ? event.lowestStep : event.highestStep;
        staff.stems[stem.stemIndex].toStep = line.stepAt(stem.x);
    }

    // The primary beam, spanning the whole group.
    const double inner = up ? -beamThicknessSteps : beamThicknessSteps;

    staff.beams.push_back(PlacedBeam{
        line.startX, line.endX, line.startStep, line.endStep, line.startStep + inner,
        line.endStep + inner, 0});

    // Secondary beams, inward, each spanning only the events that carry it.
    int level = 1;

    const auto flagsAt = [&prepared, &events](std::size_t position)
    { return prepared[events[position]].value.flags; };

    while (true)
    {
        int deepest = 0;

        for (std::size_t position = 0; position < events.size(); ++position)
        {
            deepest = std::max(deepest, flagsAt(position));
        }

        if (level >= deepest)
        {
            break;
        }

        const double offset = (up ? -1.0 : 1.0) * beamPitchSteps * level;

        std::size_t runStart = 0;
        bool inRun = false;

        const auto emit = [&](std::size_t first, std::size_t last)
        {
            double leftX = stems[events[first]].x;
            double rightX = stems[events[last]].x;

            if (first == last)
            {
                // A lone sixteenth inside a beamed group -- the short half of a
                // dotted rhythm. It has nothing to beam to, so it gets a stub
                // pointing back toward its neighbour, which is what says it is
                // a sixteenth rather than an eighth.
                if (first > 0)
                {
                    leftX = rightX - beamStubWidth;
                }
                else
                {
                    rightX = leftX + beamStubWidth;
                }
            }

            const double leftOuter = line.stepAt(leftX) + offset;
            const double rightOuter = line.stepAt(rightX) + offset;

            staff.beams.push_back(PlacedBeam{
                leftX, rightX, leftOuter, rightOuter, leftOuter + inner, rightOuter + inner,
                level});
        };

        for (std::size_t position = 0; position < events.size(); ++position)
        {
            const bool carries = flagsAt(position) > level;

            if (carries && !inRun)
            {
                runStart = position;
                inRun = true;
            }
            else if (!carries && inRun)
            {
                emit(runStart, position - 1);
                inRun = false;
            }
        }

        if (inRun)
        {
            emit(runStart, events.size() - 1);
        }

        ++level;
    }
}

// One column: a distinct absolute onset, and what it needs.
struct Column
{
    Tick onset = 0;
    double width = 0.0;

    // Offset from the start of its measure, filled in during placement.
    double x = 0.0;
};

// The columns of one measure, across every part and staff.
struct MeasureColumns
{
    std::vector<Column> columns;
    double width = 0.0;
};
} // namespace

double spacingForDuration(Tick duration, const LayoutOptions& options) noexcept
{
    if (duration <= 0)
    {
        return options.quarterNoteWidth * 0.5;
    }

    const double quarters =
        static_cast<double>(duration) / static_cast<double>(ticksPerQuarterNote);

    return options.quarterNoteWidth * std::pow(quarters, spacingExponent);
}

ScoreLayout layOutScore(const Score& score, const LayoutOptions& options)
{
    ScoreLayout layout;
    layout.width = options.availableWidth;

    if (score.parts.empty())
    {
        return layout;
    }

    const std::size_t measures = measureCount(score);

    if (measures == 0)
    {
        return layout;
    }

    // Pass one: everything independent of the available width.
    std::vector<std::vector<PreparedEvent>> preparedByPart;
    preparedByPart.reserve(score.parts.size());

    std::vector<PartAttributeTrack> attributesByPart(score.parts.size());
    std::vector<std::vector<BeamGroup>> beamGroupsByPart(score.parts.size());

    for (std::size_t partIndex = 0; partIndex < score.parts.size(); ++partIndex)
    {
        preparedByPart.push_back(prepare(
            score.parts[partIndex], partIndex, attributesByPart[partIndex],
            beamGroupsByPart[partIndex], layout.diagnostics));
    }

    // The head is as wide as the widest staff's head, so that music starts at
    // one x across the whole system -- a grand staff whose bass clef key
    // signature is narrower than its treble one must still line up. Computed
    // for every measure, because any measure may turn out to begin a system and
    // the width is an input to deciding which ones do.
    std::vector<double> headWidthAtMeasure(measures, 0.0);

    for (std::size_t measureIndex = 0; measureIndex < measures; ++measureIndex)
    {
        for (const PartAttributeTrack& attributes : attributesByPart)
        {
            if (measureIndex >= attributes.clefs.size())
            {
                continue;
            }

            for (const Clef& clef : attributes.clefs[measureIndex])
            {
                const SystemHead head = systemHeadFor(
                    clef, attributes.keys[measureIndex], attributes.times[measureIndex]);

                headWidthAtMeasure[measureIndex] =
                    std::max(headWidthAtMeasure[measureIndex], head.width);
            }
        }
    }

    // Pass two: columns per measure, shared by every staff of every part. This
    // is what makes equal onsets land at equal x.
    std::vector<MeasureColumns> measureColumns(measures);

    for (const std::vector<PreparedEvent>& prepared : preparedByPart)
    {
        for (const PreparedEvent& event : prepared)
        {
            MeasureColumns& columns = measureColumns[event.measureIndex];

            const auto existing = std::find_if(
                columns.columns.begin(), columns.columns.end(),
                [&event](const Column& column) { return column.onset == event.absoluteOnset; });

            if (existing == columns.columns.end())
            {
                columns.columns.push_back(Column{event.absoluteOnset, event.width, 0.0});
            }
            else
            {
                existing->width = std::max(existing->width, event.width);
            }
        }
    }

    const Part& firstPart = score.parts.front();

    for (std::size_t measureIndex = 0; measureIndex < measures; ++measureIndex)
    {
        MeasureColumns& columns = measureColumns[measureIndex];

        std::sort(
            columns.columns.begin(), columns.columns.end(),
            [](const Column& lhs, const Column& rhs) { return lhs.onset < rhs.onset; });

        const Measure& measure = firstPart.measures[measureIndex];
        const Tick measureEnd = measure.start + measure.nominalDuration;

        double x = 0.0;

        for (std::size_t index = 0; index < columns.columns.size(); ++index)
        {
            Column& column = columns.columns[index];

            const Tick next =
                index + 1 < columns.columns.size() ? columns.columns[index + 1].onset : measureEnd;

            // The larger of what the music asks for and what the glyphs need.
            column.width = std::max(
                spacingForDuration(next - column.onset, options), column.width + glyphPadding);
            column.x = x;
            x += column.width;
        }

        // An empty measure still occupies a bar's worth of room.
        columns.width =
            (columns.columns.empty() ? spacingForDuration(measure.nominalDuration, options) : x) +
            barlinePadding;
    }

    // Pass three: greedy system breaking.
    struct SystemRange
    {
        std::size_t first = 0;
        std::size_t count = 0;
    };

    std::vector<SystemRange> ranges;

    {
        SystemRange current;
        double used = headWidthAtMeasure.front();

        for (std::size_t measureIndex = 0; measureIndex < measures; ++measureIndex)
        {
            const double width = measureColumns[measureIndex].width;

            const bool fits = current.count == 0 || used + width <= options.availableWidth;

            if (!fits)
            {
                ranges.push_back(current);

                current = SystemRange{measureIndex, 0};
                used = headWidthAtMeasure[measureIndex];
            }

            if (current.count == 0)
            {
                current.first = measureIndex;
            }

            ++current.count;
            used += width;
        }

        if (current.count > 0)
        {
            ranges.push_back(current);
        }
    }

    // Pass four: place.
    double systemTop = 0.0;

    for (const SystemRange& range : ranges)
    {
        SystemLayout system;
        system.topOffset = systemTop;
        system.firstMeasureIndex = range.first;
        system.measureCount = range.count;

        const double headWidth = headWidthAtMeasure[range.first];

        // Where each measure starts within this system.
        std::vector<double> measureX(range.count, 0.0);

        {
            double x = headWidth;

            for (std::size_t offset = 0; offset < range.count; ++offset)
            {
                measureX[offset] = x;
                x += measureColumns[range.first + offset].width;

                system.barlineXs.push_back(x - barlinePadding * 0.5);
            }
        }

        double staffTop = 0.0;

        for (std::size_t partIndex = 0; partIndex < score.parts.size(); ++partIndex)
        {
            const Part& part = score.parts[partIndex];
            const std::size_t staffCountForPart =
                static_cast<std::size_t>(std::max(1, part.staffCount));

            PartLayout partLayout;
            partLayout.partIndex = partIndex;
            partLayout.braced = staffCountForPart > 1;

            for (std::size_t staffIndex = 0; staffIndex < staffCountForPart; ++staffIndex)
            {
                StaffLayout staff;
                staff.staffNumber = static_cast<int>(staffIndex) + 1;
                staff.topOffset = staffTop;

                // Clef, key and time signature at the head of the system, drawn
                // on every system rather than only the first -- a reader who
                // scrolls to the middle of a page needs them there.
                const PartAttributeTrack& attributes = attributesByPart[partIndex];

                if (range.first < attributes.clefs.size() &&
                    staffIndex < attributes.clefs[range.first].size())
                {
                    const std::vector<Clef>& clefs = attributes.clefs[range.first];

                    const SystemHead head = systemHeadFor(
                        clefs[staffIndex], attributes.keys[range.first],
                        attributes.times[range.first]);

                    staff.glyphs.insert(staff.glyphs.end(), head.glyphs.begin(), head.glyphs.end());
                }

                partLayout.staves.push_back(staff);

                // A five-line staff occupies four staff spaces between its
                // outer lines; the gap is measured from the bottom line of one
                // to the top line of the next.
                staffTop += staffHeight;

                if (staffIndex + 1 < staffCountForPart)
                {
                    staffTop += options.staffGapWithinPart;
                }
            }

            // The brace spans from the top line of the first staff to the
            // bottom line of the last -- the whole part, which is what makes
            // two staves read as one instrument rather than as two.
            if (partLayout.braced && !partLayout.staves.empty())
            {
                partLayout.braceX = 0.0;
                partLayout.braceTop = partLayout.staves.front().topOffset;
                partLayout.braceBottom = partLayout.staves.back().topOffset + staffHeight;
            }

            system.parts.push_back(partLayout);

            staffTop += options.gapBetweenParts;
        }

        // Now the music itself, into the staff each event names.
        //
        // Where each event's stem landed, so the beam pass can find it again.
        // A beam is placed after every stem in its group exists, because it has
        // to move them.
        std::vector<std::vector<PlacedStemRef>> stemRefByPart(preparedByPart.size());

        for (std::size_t sourcePart = 0; sourcePart < preparedByPart.size(); ++sourcePart)
        {
            const std::vector<PreparedEvent>& prepared = preparedByPart[sourcePart];

            stemRefByPart[sourcePart].assign(prepared.size(), PlacedStemRef{});

            for (std::size_t eventIndex = 0; eventIndex < prepared.size(); ++eventIndex)
            {
                const PreparedEvent& event = prepared[eventIndex];

                if (event.measureIndex < range.first ||
                    event.measureIndex >= range.first + range.count)
                {
                    continue;
                }

                const MeasureColumns& columns = measureColumns[event.measureIndex];

                const auto column = std::find_if(
                    columns.columns.begin(), columns.columns.end(),
                    [&event](const Column& candidate)
                    { return candidate.onset == event.absoluteOnset; });

                if (column == columns.columns.end())
                {
                    continue;
                }

                const double x = measureX[event.measureIndex - range.first] + column->x;

                PartLayout& partLayout = system.parts[event.partIndex];

                const std::size_t staffIndex =
                    static_cast<std::size_t>(event.staff - 1) < partLayout.staves.size()
                        ? static_cast<std::size_t>(event.staff - 1)
                        : 0;

                StaffLayout& staff = partLayout.staves[staffIndex];

                if (event.isRest)
                {
                    staff.glyphs.push_back(PlacedGlyph{event.restGlyph, x, event.restStaffStep});

                    placeDots(
                        staff, x + advanceWidth(event.restGlyph), event.restStaffStep,
                        event.value.dots);

                    continue;
                }

                // One x for every notehead of the chord, so that the stem has
                // one line to attach to. The accidental shifts the notehead
                // right, and every note of the chord shifts with it.
                double noteheadX = x;

                for (const PreparedNote& note : event.notes)
                {
                    if (note.accidental.has_value())
                    {
                        noteheadX =
                            std::max(noteheadX, x + advanceWidth(*note.accidental) + glyphPadding);
                    }
                }

                for (const PreparedNote& note : event.notes)
                {
                    if (note.accidental.has_value())
                    {
                        staff.glyphs.push_back(PlacedGlyph{*note.accidental, x, note.staffStep});
                    }

                    staff.glyphs.push_back(PlacedGlyph{note.notehead, noteheadX, note.staffStep});

                    const LedgerLineRange ledgers = ledgerLinesFor(note.staffStep);

                    for (int step = ledgers.beginStep; step != ledgers.endStep;
                         step += ledgers.stride)
                    {
                        staff.ledgerLines.push_back(PlacedLedgerLine{
                            noteheadX - engravingDefaults.legerLineExtension,
                            advanceWidth(note.notehead) +
                                (2 * engravingDefaults.legerLineExtension),
                            step});
                    }
                }

                const Glyph notehead = event.notes.front().notehead;

                // One stem for the whole chord, from its outermost notehead --
                // never one per note.
                if (event.hasStem)
                {
                    const StemGeometry geometry = stemFor(
                        noteheadX, notehead, event.lowestStep, event.highestStep, event.stemUp);

                    staff.stems.push_back(
                        PlacedStem{geometry.x, geometry.fromStep, geometry.toStep});

                    stemRefByPart[sourcePart][eventIndex] = PlacedStemRef{
                        true, staffIndex, staff.stems.size() - 1, geometry.x, geometry.toStep};

                    // A flag only where no beam replaced it. The glyph sits at
                    // the free end of the stem, on the stem's own x.
                    if (event.value.flags > 0 && !event.beamed)
                    {
                        staff.glyphs.push_back(PlacedGlyph{
                            flagGlyphFor(event.value.flags, event.stemUp), geometry.x,
                            static_cast<int>(geometry.toStep)});
                    }
                }

                // Dots go after the chord, at each notehead's own step.
                for (const PreparedNote& note : event.notes)
                {
                    placeDots(
                        staff, noteheadX + advanceWidth(note.notehead), note.staffStep,
                        event.value.dots);
                }
            }
        }

        // Beams, last: every stem they move is now placed.
        for (std::size_t sourcePart = 0; sourcePart < beamGroupsByPart.size(); ++sourcePart)
        {
            for (const BeamGroup& group : beamGroupsByPart[sourcePart])
            {
                placeBeam(
                    preparedByPart[sourcePart], group.events, stemRefByPart[sourcePart],
                    system.parts[sourcePart]);
            }
        }

        system.height = staffTop - options.gapBetweenParts;

        systemTop += system.height + options.systemGap;

        layout.systems.push_back(system);
    }

    layout.height = systemTop > 0.0 ? systemTop - options.systemGap : 0.0;

    return layout;
}
} // namespace score::engraving
