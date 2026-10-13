#include "ScoreLayout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "Accidentals.h"
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

    // Horizontal room this event's glyphs need, accidentals included.
    double width = 0.0;
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
    // counting it is how a flag ends up drawn across the next column.
    if (event.hasStem && event.value.flags > 0 && event.stemUp)
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

            entry.width = widthOf(entry);

            prepared.push_back(entry);
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

    for (std::size_t partIndex = 0; partIndex < score.parts.size(); ++partIndex)
    {
        preparedByPart.push_back(prepare(
            score.parts[partIndex], partIndex, attributesByPart[partIndex], layout.diagnostics));
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
        for (const std::vector<PreparedEvent>& prepared : preparedByPart)
        {
            for (const PreparedEvent& event : prepared)
            {
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

                    // A flag, until beams exist to replace it. The glyph sits
                    // at the free end of the stem, on the stem's own x.
                    if (event.value.flags > 0)
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

        system.height = staffTop - options.gapBetweenParts;

        systemTop += system.height + options.systemGap;

        layout.systems.push_back(system);
    }

    layout.height = systemTop > 0.0 ? systemTop - options.systemGap : 0.0;

    return layout;
}
} // namespace score::engraving
