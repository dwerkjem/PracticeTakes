#include "ScoreLayoutEngine.h"

#include "BeamGeometry.h"
#include "BeamGrouping.h"
#include "KeySignatureLayout.h"
#include "MeasureAccidentals.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

namespace engraving
{
namespace
{
// Extra room before a measure's first note when it carries a clef, key or time
// signature, so the signature does not touch the notehead.
constexpr double headerGap = 0.9;

// Room after a measure's opening barline, before anything is drawn in it. A
// notehead hard against a barline reads as part of it.
constexpr double contentLeadIn = 0.8;

// Room a repeat barline takes at the start of a measure.
constexpr double repeatBarlineWidth = 1.3;

// One note of one event, already positioned vertically.
struct PlacedNote
{
    score::NoteRef ref;
    StaffSteps position = 0;
    AccidentalKind accidental = AccidentalKind::none;

    // Nudged right when the note sits a second away from its neighbour in the
    // same chord, which is the one case two noteheads would otherwise overlap.
    double xOffset = 0.0;

    bool startsTie = false;
};

struct PlacedEvent
{
    const score::ScoreEvent* event = nullptr;

    int voiceNumber = 1;

    // Relative to the start of the measure's content, in staff spaces.
    double localX = 0.0;

    std::optional<WrittenNoteValue> value;

    std::vector<PlacedNote> notes;

    bool stemUp = true;
    bool isRest = false;

    // Which beam group this event belongs to, or `noBeamGroup`. A beamed event
    // draws no flags: the beam stands in for them.
    std::size_t beamGroup = 0;
    bool beamed = false;
};

// One measure, with its attributes resolved and its events placed in
// measure-local coordinates. Built before anything knows how wide the tool is,
// because none of it depends on that.
struct MeasurePlan
{
    std::size_t index = 0;
    std::string printedNumber;

    score::Clef clef;
    score::KeySignature key;
    score::TimeSignature time;

    bool clefChangesHere = false;
    bool keyChangesHere = false;
    bool timeChangesHere = false;

    bool repeatStart = false;
    bool repeatEnd = false;

    double contentWidth = 0.0;

    std::vector<PlacedEvent> events;

    // One entry per beam group, holding its members as indices into `events`
    // in reading order. Resolved before the horizontal pass, because a beamed
    // group takes one stem direction for all of its notes and that decides
    // nothing about x but everything about where the beam goes.
    std::vector<std::vector<std::size_t>> beamGroups;

    // Dynamics and tempo marks, with their measure-local x.
    std::vector<TextGlyph> texts;
};

// Collects notices without repeating one. The same gap in forty measures is one
// sentence to a reader.
class NoticeList
{
  public:
    void add(std::string notice)
    {
        if (std::find(ordered.begin(), ordered.end(), notice) == ordered.end())
        {
            ordered.push_back(std::move(notice));
        }
    }

    [[nodiscard]] std::vector<std::string> take()
    {
        return std::move(ordered);
    }

  private:
    std::vector<std::string> ordered;
};

[[nodiscard]] std::string pluralised(std::size_t count, const std::string& singular)
{
    return std::to_string(count) + " " + singular + (count == 1 ? "" : "s");
}

// Every event of a measure that sits on `staff`, in the order a reader meets
// them: by onset, then by voice, then as written.
struct EventInMeasure
{
    const score::ScoreEvent* event = nullptr;
    std::size_t voiceIndex = 0;
    std::size_t eventIndex = 0;
    int voiceNumber = 1;
};

[[nodiscard]] std::vector<EventInMeasure>
eventsOnStaff(const score::Measure& measure, int staff, std::size_t& skippedOnOtherStaves)
{
    std::vector<EventInMeasure> collected;

    for (std::size_t voiceIndex = 0; voiceIndex < measure.voices.size(); ++voiceIndex)
    {
        const score::Voice& voice = measure.voices.at(voiceIndex);

        for (std::size_t eventIndex = 0; eventIndex < voice.events.size(); ++eventIndex)
        {
            const score::ScoreEvent& event = voice.events.at(eventIndex);

            if (event.staff != staff)
            {
                ++skippedOnOtherStaves;

                continue;
            }

            collected.push_back(EventInMeasure{&event, voiceIndex, eventIndex, voice.number});
        }
    }

    // Stable within a voice already, so sorting on the two outer keys keeps the
    // written order of a voice's own events.
    std::stable_sort(
        collected.begin(), collected.end(),
        [](const EventInMeasure& lhs, const EventInMeasure& rhs)
        {
            if (lhs.event->onset != rhs.event->onset)
            {
                return lhs.event->onset < rhs.event->onset;
            }

            return lhs.voiceNumber < rhs.voiceNumber;
        });

    return collected;
}

// Where a rest of this value is drawn. A whole rest hangs from the fourth line
// and a half rest sits on the middle line; everything shorter centres on the
// middle line. The painter draws the shape, this says against which line.
[[nodiscard]] StaffSteps restPositionFor(WrittenValue value)
{
    switch (value)
    {
    case WrittenValue::breve:
    case WrittenValue::whole:
        return stepsForLineFromBottom(4);
    default:
        return middleLineSteps;
    }
}

// Horizontal offsets for the notes of one chord. Two notes a second apart
// cannot share an x, so the upper of the pair moves one notehead to the right --
// on either stem side, because a first pass that chose the correct side per
// stem direction would be guessing at the rest of the collision problem too.
void offsetSeconds(std::vector<PlacedNote>& notes, double noteheadWidth)
{
    if (notes.size() < 2)
    {
        return;
    }

    std::vector<PlacedNote*> byPosition;
    byPosition.reserve(notes.size());

    for (PlacedNote& note : notes)
    {
        byPosition.push_back(&note);
    }

    std::sort(
        byPosition.begin(), byPosition.end(),
        [](const PlacedNote* lhs, const PlacedNote* rhs) { return lhs->position > rhs->position; });

    for (std::size_t index = 1; index < byPosition.size(); ++index)
    {
        const StaffSteps gap = byPosition.at(index - 1)->position - byPosition.at(index)->position;

        // A second is one half space. Only shift when the previous note is not
        // itself already shifted, so a cluster alternates rather than marching
        // off to the right.
        if (gap == 1 && byPosition.at(index - 1)->xOffset == 0.0)
        {
            byPosition.at(index)->xOffset = noteheadWidth;
        }
    }
}

// The voice numbers a measure's placed events use, in the order they were met.
[[nodiscard]] std::vector<int> voiceNumbersOf(const std::vector<PlacedEvent>& events)
{
    std::vector<int> numbers;

    for (const PlacedEvent& placed : events)
    {
        if (std::find(numbers.begin(), numbers.end(), placed.voiceNumber) == numbers.end())
        {
            numbers.push_back(placed.voiceNumber);
        }
    }

    return numbers;
}

// Beams this event carries: none unless it is a pitched, non-grace note of an
// eighth or shorter. A grace note is drawn with flags in this pass, which is
// the one gap beaming leaves behind and the notice below says so.
[[nodiscard]] int beamLevelsOf(const PlacedEvent& placed)
{
    if (placed.isRest || placed.notes.empty() || placed.event->isGrace || !placed.value.has_value())
    {
        return 0;
    }

    return flagCountFor(placed.value->value);
}

// Beam direction for a whole group: the note furthest from the middle line
// decides, which is the convention and is the reason a group is not just every
// note's own direction agreeing by luck. A tie goes down, as an engraver's
// does.
[[nodiscard]] bool
groupStemPointsUp(const std::vector<PlacedEvent>& events, const std::vector<std::size_t>& group)
{
    StaffSteps highest = middleLineSteps;
    StaffSteps lowest = middleLineSteps;
    bool anyNote = false;

    for (const std::size_t index : group)
    {
        for (const PlacedNote& note : events.at(index).notes)
        {
            if (!anyNote)
            {
                highest = note.position;
                lowest = note.position;
                anyNote = true;

                continue;
            }

            highest = std::min(highest, note.position);
            lowest = std::max(lowest, note.position);
        }
    }

    const int distanceAbove = middleLineSteps - highest;
    const int distanceBelow = lowest - middleLineSteps;

    return distanceBelow > distanceAbove;
}

// Resolve a measure's beam groups, one voice at a time, and report the runs
// beaming does not reach.
void resolveBeamGroups(MeasurePlan& plan, NoticeList& notices)
{
    const score::Tick groupTicks = beamGroupTicks(plan.time);

    for (const int voiceNumber : voiceNumbersOf(plan.events))
    {
        std::vector<BeamCandidate> candidates;
        std::vector<std::size_t> eventIndices;

        for (std::size_t index = 0; index < plan.events.size(); ++index)
        {
            const PlacedEvent& placed = plan.events.at(index);

            if (placed.voiceNumber != voiceNumber)
            {
                continue;
            }

            candidates.push_back(
                BeamCandidate{placed.event->onset, placed.event->duration, beamLevelsOf(placed)});
            eventIndices.push_back(index);
        }

        for (const std::vector<std::size_t>& group : beamGroupsFor(candidates, plan.time))
        {
            std::vector<std::size_t> members;
            members.reserve(group.size());

            for (const std::size_t candidateIndex : group)
            {
                members.push_back(eventIndices.at(candidateIndex));
            }

            for (const std::size_t member : members)
            {
                plan.events.at(member).beamed = true;
                plan.events.at(member).beamGroup = plan.beamGroups.size();
            }

            plan.beamGroups.push_back(std::move(members));
        }

        // What beaming did not reach, reported rather than left to be noticed
        // as a page that beams inconsistently. A rest inside a beat with a
        // beamable note either side of it is the case an engraver would beam
        // over and this pass does not.
        for (std::size_t index = 1; groupTicks > 0 && index + 1 < candidates.size(); ++index)
        {
            const BeamCandidate& before = candidates.at(index - 1);
            const BeamCandidate& candidate = candidates.at(index);
            const BeamCandidate& after = candidates.at(index + 1);

            if (candidate.beamLevels > 0 || before.beamLevels == 0 || after.beamLevels == 0)
            {
                continue;
            }

            const score::Tick group = candidate.onset / groupTicks;

            if (before.onset / groupTicks == group && after.onset / groupTicks == group)
            {
                notices.add("A beam is not carried over a rest, so a beat holding one is drawn "
                            "with flags either side of it.");
            }
        }

        std::size_t consecutiveGraceNotes = 0;

        for (const std::size_t index : eventIndices)
        {
            const PlacedEvent& placed = plan.events.at(index);

            consecutiveGraceNotes = placed.event->isGrace ? consecutiveGraceNotes + 1 : 0;

            if (consecutiveGraceNotes > 1)
            {
                notices.add("A run of grace notes is drawn with flags; grace notes are not "
                            "beamed in this pass.");

                break;
            }
        }
    }
}

// One onset's worth of events, and the room needed to the left of it.
struct Slot
{
    score::Tick onset = 0;
    double leftPad = 0.0;
    double x = 0.0;
    std::vector<std::size_t> graceEvents;
    std::vector<std::size_t> mainEvents;
};

// Place a measure's events: vertical positions and accidentals first, because
// the accidental state machine runs in reading order across every voice, then
// horizontal positions from the onsets.
void planMeasure(
    MeasurePlan& plan,
    const score::Measure& measure,
    int staff,
    const LayoutOptions& options,
    NoticeList& notices,
    int& lyricVerseCount,
    std::size_t& skippedOnOtherStaves)
{
    const std::vector<EventInMeasure> inOrder = eventsOnStaff(measure, staff, skippedOnOtherStaves);

    MeasureAccidentals accidentals(plan.key);

    std::vector<int> voiceNumbersSeen;

    for (const EventInMeasure& entry : inOrder)
    {
        const score::ScoreEvent& event = *entry.event;

        PlacedEvent placed;
        placed.event = &event;
        placed.voiceNumber = entry.voiceNumber;
        placed.isRest = !score::isPitched(event);

        if (std::find(voiceNumbersSeen.begin(), voiceNumbersSeen.end(), entry.voiceNumber) ==
            voiceNumbersSeen.end())
        {
            voiceNumbersSeen.push_back(entry.voiceNumber);
        }

        // A grace note has no duration to derive a written value from
        // (invariant 3), so the convention is applied instead: grace notes are
        // drawn as eighths unless a file says otherwise, and the model does not
        // carry what it said.
        placed.value =
            event.isGrace
                ? std::optional<WrittenNoteValue>{WrittenNoteValue{WrittenValue::eighth, 0}}
                : writtenValueFor(event);

        if (!placed.value.has_value())
        {
            notices.add(
                "Some durations in this part match no written note value and are drawn as "
                "quarter notes. Bar " +
                (plan.printedNumber.empty() ? std::to_string(plan.index + 1) : plan.printedNumber) +
                " is the first.");

            placed.value = WrittenNoteValue{WrittenValue::quarter, 0};
        }

        for (std::size_t noteIndex = 0; noteIndex < event.notes.size(); ++noteIndex)
        {
            const score::Note& note = event.notes.at(noteIndex);

            const std::optional<StaffSteps> position = staffPositionFor(plan.clef, note.written);

            if (!position.has_value())
            {
                // Guarded before the measure is planned at all; reaching here
                // would mean the clef changed to an unpitched one mid-part.
                notices.add(
                    "This part changes to a clef that names no pitch, and the notes under it are "
                    "not drawn.");

                continue;
            }

            const bool isTieContinuation = note.tiedFrom.has_value();

            placed.notes.push_back(PlacedNote{
                score::NoteRef{plan.index, entry.voiceIndex, entry.eventIndex, noteIndex},
                *position, accidentals.accidentalFor(note.written, isTieContinuation), 0.0,
                note.tiedTo.has_value()});
        }

        offsetSeconds(placed.notes, options.noteheadWidth);

        for (const score::LyricSyllable& syllable : event.lyrics)
        {
            lyricVerseCount = std::max(lyricVerseCount, syllable.verse);
        }

        plan.events.push_back(std::move(placed));
    }

    // Which notes beam together, before stem directions: a beamed group takes
    // one direction for all of its notes, so the group has to be known first.
    resolveBeamGroups(plan, notices);

    if (voiceNumbersSeen.size() > 1)
    {
        // Two voices on one staff read by stem direction, not by pitch: voice
        // one up, everything else down. See design.md decision 6. A beam group
        // never spans two voices, so this already gives every group one
        // direction and nothing below needs to override it.
        for (PlacedEvent& placed : plan.events)
        {
            placed.stemUp = placed.voiceNumber == voiceNumbersSeen.front();
        }
    }
    else
    {
        for (PlacedEvent& placed : plan.events)
        {
            if (placed.notes.empty())
            {
                continue;
            }

            // By the lowest note of a chord, which is what decides the
            // direction for the group.
            const auto lowest = std::max_element(
                placed.notes.begin(), placed.notes.end(),
                [](const PlacedNote& lhs, const PlacedNote& rhs)
                { return lhs.position < rhs.position; });

            placed.stemUp = stemPointsUpFor(lowest->position);
        }

        // A beam joins stems pointing the same way, so a group overrules every
        // note's own direction with one of its own.
        for (const std::vector<std::size_t>& group : plan.beamGroups)
        {
            const bool up = groupStemPointsUp(plan.events, group);

            for (const std::size_t index : group)
            {
                plan.events.at(index).stemUp = up;
            }
        }
    }

    // Horizontal. One slot per distinct onset, in order.
    std::vector<Slot> slots;

    for (std::size_t index = 0; index < plan.events.size(); ++index)
    {
        const PlacedEvent& placed = plan.events.at(index);
        const score::Tick onset = placed.event->onset;

        if (slots.empty() || slots.back().onset != onset)
        {
            slots.push_back(Slot{onset, 0.0, 0.0, {}, {}});
        }

        Slot& slot = slots.back();

        if (placed.event->isGrace)
        {
            slot.graceEvents.push_back(index);
            slot.leftPad += options.graceNoteWidth;
        }
        else
        {
            slot.mainEvents.push_back(index);
        }

        const bool anyAccidental = std::any_of(
            placed.notes.begin(), placed.notes.end(),
            [](const PlacedNote& note) { return note.accidental != AccidentalKind::none; });

        if (anyAccidental)
        {
            slot.leftPad = std::max(slot.leftPad, options.accidentalWidth);
        }
    }

    const double stretchPerTick =
        options.durationStretchPerQuarter / static_cast<double>(score::ticksPerQuarterNote);

    // A flag hangs off the right of an up stem, so the least room between two
    // onsets has to clear a notehead and a flag together. Derived rather than
    // left to `minimumSlotWidth` alone, so that lowering one number cannot
    // quietly let a flagged eighth touch the note after it.
    const double slotFloor =
        std::max(options.minimumSlotWidth, options.noteheadWidth + options.flagWidth);

    double cursor = 0.0;

    for (std::size_t index = 0; index < slots.size(); ++index)
    {
        Slot& slot = slots.at(index);

        const double proportional = static_cast<double>(slot.onset) * stretchPerTick;

        if (index == 0)
        {
            slot.x = slot.leftPad;
        }
        else
        {
            slot.x = std::max(proportional + slot.leftPad, cursor + slotFloor + slot.leftPad);
        }

        cursor = slot.x;

        for (const std::size_t eventIndex : slot.mainEvents)
        {
            plan.events.at(eventIndex).localX = slot.x;
        }

        // Grace notes sit immediately before the beat they lead into, in the
        // room `leftPad` reserved for them, and take no time of their own.
        const std::size_t graceCount = slot.graceEvents.size();

        for (std::size_t graceIndex = 0; graceIndex < graceCount; ++graceIndex)
        {
            plan.events.at(slot.graceEvents.at(graceIndex)).localX =
                slot.x - (static_cast<double>(graceCount - graceIndex) * options.graceNoteWidth);
        }
    }

    // A measure is as wide as its musical length, not as its note count: a bar
    // of four quarters holding one whole rest is still a bar of four quarters.
    score::Tick measureDuration = measure.nominalDuration;

    for (const PlacedEvent& placed : plan.events)
    {
        measureDuration = std::max(measureDuration, score::endOf(*placed.event));
    }

    const score::Tick lastOnset = slots.empty() ? 0 : slots.back().onset;
    const double trailing =
        static_cast<double>(std::max<score::Tick>(measureDuration - lastOnset, 0)) * stretchPerTick;

    plan.contentWidth = cursor + std::max(trailing, slotFloor) + options.measureTrailingWidth;

    // A measure whose only content is a rest covering it centres that rest,
    // which is the convention and is what makes an empty bar legible.
    if (plan.events.size() == 1 && plan.events.front().isRest &&
        plan.events.front().event->duration >= measure.nominalDuration &&
        measure.nominalDuration > 0)
    {
        plan.events.front().localX = plan.contentWidth * 0.5;
    }

    // Tempo and dynamic markings, placed by the same proportional rule as the
    // notes so that a marking lands under the note it was attached to.
    for (const score::Direction& direction : measure.directions)
    {
        if (direction.staff != staff)
        {
            continue;
        }

        const double x =
            std::min(static_cast<double>(direction.onset) * stretchPerTick, plan.contentWidth);

        if (direction.kind == score::DirectionKind::dynamic)
        {
            if (!direction.text.empty())
            {
                plan.texts.push_back(TextGlyph{x, direction.text, TextRole::dynamic});
            }

            continue;
        }

        std::string label = direction.text;

        if (direction.beatsPerMinute.has_value())
        {
            // Whole beats per minute: a tempo mark reads as a number, and the
            // model keeps the exact value for whatever plays it.
            const long long rounded = std::llround(*direction.beatsPerMinute);

            if (!label.empty())
            {
                label += "  ";
            }

            label += std::to_string(rounded);
            label += " bpm";
        }

        if (!label.empty())
        {
            plan.texts.push_back(TextGlyph{x, label, TextRole::tempo});
        }
    }
}

// Room a measure needs before its first note, for whatever signature appears
// there. Clef and key repeat at every system start, which is the convention; a
// time signature does not.
[[nodiscard]] double
headerWidthFor(const MeasurePlan& plan, bool startsSystem, const LayoutOptions& options)
{
    double width = 0.0;

    if (plan.repeatStart)
    {
        width += repeatBarlineWidth;
    }

    if (startsSystem || plan.clefChangesHere)
    {
        width += options.clefWidth;
    }

    if (startsSystem || plan.keyChangesHere)
    {
        width +=
            static_cast<double>(keySignatureAccidentalCount(plan.key)) * options.keyAccidentalWidth;
    }

    if (plan.timeChangesHere)
    {
        width += options.timeSignatureWidth;
    }

    return (width > 0.0 ? width + headerGap : 0.0) + contentLeadIn;
}

// Where every notehead ended up, so ties can be resolved once the systems are
// known. Keyed by the note's own identity in the model.
struct NotePlacement
{
    std::size_t systemIndex = 0;
    double x = 0.0;
    StaffSteps position = 0;
    bool stemUp = true;
};

// An ordering for the map below. A free `operator<` would not be found: the
// model's NoteRef has no ordering of its own, and one declared here is not in a
// namespace associated with it, so `std::less` could never reach it.
struct NoteRefLess
{
    [[nodiscard]] bool operator()(const score::NoteRef& lhs, const score::NoteRef& rhs) const
    {
        return std::tie(lhs.measureIndex, lhs.voiceIndex, lhs.eventIndex, lhs.noteIndex) <
               std::tie(rhs.measureIndex, rhs.voiceIndex, rhs.eventIndex, rhs.noteIndex);
    }
};

using NotePlacementMap = std::map<score::NoteRef, NotePlacement, NoteRefLess>;

// Lay one measure's glyphs into a system, starting at `startX`, and return the
// x its barline sits at.
double emitMeasure(
    System& system,
    std::size_t systemIndex,
    const MeasurePlan& plan,
    bool startsSystem,
    bool isLastMeasure,
    const LayoutOptions& options,
    NotePlacementMap& placements,
    double startX)
{
    double cursor = startX;

    if (plan.repeatStart)
    {
        system.barlines.push_back(BarlineGlyph{cursor, BarlineKind::repeatStart});
        cursor += repeatBarlineWidth;
    }

    if (startsSystem || plan.clefChangesHere)
    {
        system.clefs.push_back(
            ClefGlyph{cursor, plan.clef, stepsForLineFromBottom(plan.clef.line)});
        cursor += options.clefWidth;
    }

    if (startsSystem || plan.keyChangesHere)
    {
        for (const KeySignatureAccidental& accidental :
             keySignatureAccidentals(plan.clef, plan.key))
        {
            system.keyAccidentals.push_back(
                KeyAccidentalGlyph{cursor, accidental.position, accidental.kind});
            cursor += options.keyAccidentalWidth;
        }
    }

    if (plan.timeChangesHere)
    {
        system.timeSignatures.push_back(
            TimeSignatureGlyph{cursor, plan.time.beats, plan.time.beatType});
        cursor += options.timeSignatureWidth;
    }

    if (cursor > startX)
    {
        cursor += headerGap;
    }

    cursor += contentLeadIn;

    const double contentStart = cursor;

    if (startsSystem && !plan.printedNumber.empty())
    {
        system.texts.push_back(TextGlyph{startX, plan.printedNumber, TextRole::measureNumber});
    }

    // Where each beam group's stems landed, so the beams can be laid out once
    // every stem in the group has an x and a direction.
    std::vector<std::vector<BeamStem>> beamStems(plan.beamGroups.size());
    std::vector<std::vector<std::size_t>> beamStemIndices(plan.beamGroups.size());
    std::vector<bool> beamGroupPointsUp(plan.beamGroups.size(), true);

    for (const PlacedEvent& placed : plan.events)
    {
        const double x = contentStart + placed.localX;

        if (!placed.value.has_value())
        {
            continue;
        }

        const WrittenNoteValue& value = *placed.value;

        if (placed.isRest)
        {
            system.rests.push_back(
                RestGlyph{x, value.value, value.dots, restPositionFor(value.value)});

            continue;
        }

        for (const PlacedNote& note : placed.notes)
        {
            system.noteheads.push_back(NoteheadGlyph{
                note.ref, x + note.xOffset, note.position, noteheadFor(value.value),
                note.accidental, value.dots, ledgerLinesFor(note.position), placed.event->isGrace});

            placements.emplace(
                note.ref,
                NotePlacement{systemIndex, x + note.xOffset, note.position, placed.stemUp});
        }

        if (!hasStem(value.value) || placed.notes.empty())
        {
            continue;
        }

        // The stem attaches to the notehead at its own end of the chord. Its
        // length is measured **from that notehead**, not from the far one --
        // measuring from the far one makes a wide chord's stem grow with the
        // chord, which is not what an engraver draws.
        //
        // Two things then lengthen it: it must clear the notehead at the other
        // end by a space, and it is pulled to the middle line, which is what
        // gives a note far outside the staff a stem that reaches back to it.
        const auto positions = std::minmax_element(
            placed.notes.begin(), placed.notes.end(),
            [](const PlacedNote& lhs, const PlacedNote& rhs)
            { return lhs.position < rhs.position; });

        const StaffSteps highest = positions.first->position;
        const StaffSteps lowest = positions.second->position;

        const StaffSteps attach = placed.stemUp ? lowest : highest;
        const StaffSteps farthest = placed.stemUp ? highest : lowest;

        StaffSteps tip = placed.stemUp ? attach - options.stemLength : attach + options.stemLength;

        if (placed.stemUp)
        {
            tip = std::min(tip, static_cast<StaffSteps>(farthest - 2));
            tip = std::min(tip, middleLineSteps);
        }
        else
        {
            tip = std::max(tip, static_cast<StaffSteps>(farthest + 2));
            tip = std::max(tip, middleLineSteps);
        }

        const int beamLevels = placed.beamed ? flagCountFor(value.value) : 0;

        // Set in by half the stem's thickness, so its outer edge is flush with
        // the notehead's: right edge on the right for an up stem, left on the
        // left for a down one.
        const double inset = options.stemThickness * 0.5;
        const double stemX = placed.stemUp ? x + options.noteheadWidth - inset : x + inset;

        // A beam stands in for the flags it replaces, so a beamed stem draws
        // none. Its tip is overwritten below, once the group's slope is known.
        system.stems.push_back(StemGlyph{
            stemX, options.stemThickness, attach, spacesBelowTopLine(tip),
            placed.beamed ? 0 : flagCountFor(value.value), beamLevels, placed.stemUp,
            placed.event->isGrace});

        if (placed.beamed)
        {
            beamStems.at(placed.beamGroup).push_back(BeamStem{stemX, attach, farthest, beamLevels});
            beamStemIndices.at(placed.beamGroup).push_back(system.stems.size() - 1);
            beamGroupPointsUp.at(placed.beamGroup) = placed.stemUp;
        }
    }

    // The beams, once their stems are placed. Laying a group out needs every
    // one of its stems, which is why this is not done inside the loop above.
    for (std::size_t group = 0; group < beamStems.size(); ++group)
    {
        const BeamGroupGeometry geometry =
            layOutBeamGroup(beamStems.at(group), beamGroupPointsUp.at(group), options);

        for (std::size_t member = 0; member < geometry.stemTips.size(); ++member)
        {
            system.stems.at(beamStemIndices.at(group).at(member)).tipSpaces =
                geometry.stemTips.at(member);
        }

        for (const BeamGlyph& beam : geometry.beams)
        {
            system.beams.push_back(beam);
        }
    }

    for (const PlacedEvent& placed : plan.events)
    {
        for (const score::LyricSyllable& syllable : placed.event->lyrics)
        {
            system.lyrics.push_back(LyricGlyph{
                contentStart + placed.localX, syllable.verse, syllable.text,
                syllable.position == score::SyllabicPosition::begin ||
                    syllable.position == score::SyllabicPosition::middle,
                syllable.extend});
        }
    }

    for (const TextGlyph& text : plan.texts)
    {
        system.texts.push_back(TextGlyph{contentStart + text.x, text.text, text.role});
    }

    const double barlineX = contentStart + plan.contentWidth;

    system.barlines.push_back(BarlineGlyph{
        barlineX, plan.repeatEnd ? BarlineKind::repeatEnd
                                 : (isLastMeasure ? BarlineKind::final : BarlineKind::single)});

    return barlineX;
}

// Names of the parts this pass is not drawing, as a readable list.
[[nodiscard]] std::string otherPartNames(const std::vector<score::Part>& parts)
{
    std::string listed;

    for (std::size_t index = 1; index < parts.size(); ++index)
    {
        const score::Part& part = parts.at(index);
        const std::string name = part.name.empty() ? part.id : part.name;

        if (!listed.empty())
        {
            listed += index + 1 == parts.size() ? " and " : ", ";
        }

        listed += name;
    }

    return listed;
}

// Walk the part once, resolving the clef, key and time in force at each
// measure and placing that measure's events.
[[nodiscard]] std::vector<MeasurePlan> planPart(
    const score::Part& part,
    int staff,
    const LayoutOptions& options,
    NoticeList& notices,
    int& lyricVerseCount,
    std::size_t& skippedOnOtherStaves)
{
    // A part that declares nothing starts in treble, 4/4, no accidentals --
    // which is also what MusicXML means by an absent attribute.
    score::Clef clef;
    score::KeySignature key;
    score::TimeSignature time;

    bool anyClefDeclared = false;

    std::vector<MeasurePlan> plans;
    plans.reserve(part.measures.size());

    for (const score::Measure& measure : part.measures)
    {
        MeasurePlan plan;
        plan.index = measure.index;
        plan.printedNumber = measure.printedNumber;
        plan.repeatStart = measure.repeats.repeatStart;
        plan.repeatEnd = measure.repeats.repeatEnd;

        for (const score::Clef& declared : measure.attributes.clefs)
        {
            if (declared.staff != staff)
            {
                continue;
            }

            clef = declared;
            plan.clefChangesHere = anyClefDeclared;
            anyClefDeclared = true;
        }

        if (measure.attributes.key.has_value())
        {
            const bool cancelling = keySignatureAccidentalCount(*measure.attributes.key) == 0 &&
                                    keySignatureAccidentalCount(key) > 0;

            key = *measure.attributes.key;
            plan.keyChangesHere = true;

            if (cancelling)
            {
                notices.add(
                    "A key change that cancels accidentals is drawn as an empty key signature; "
                    "the cancelling naturals are not drawn.");
            }
        }

        if (measure.attributes.time.has_value())
        {
            time = *measure.attributes.time;
            plan.timeChangesHere = true;
        }

        plan.clef = clef;
        plan.key = key;
        plan.time = time;

        if (!measure.repeats.endingNumbers.empty())
        {
            notices.add("Ending (volta) brackets are not drawn in this pass.");
        }

        if (!measure.repeats.jumps.empty())
        {
            notices.add(
                "Jump markings -- D.C., D.S., Fine, Coda -- are carried by the score but not "
                "drawn, and nothing follows them.");
        }

        planMeasure(plan, measure, staff, options, notices, lyricVerseCount, skippedOnOtherStaves);

        plans.push_back(std::move(plan));
    }

    return plans;
}

// The clef the part begins in on `staff`, which decides whether it can be
// drawn at all.
[[nodiscard]] score::Clef openingClef(const score::Part& part, int staff)
{
    for (const score::Measure& measure : part.measures)
    {
        for (const score::Clef& clef : measure.attributes.clefs)
        {
            if (clef.staff == staff)
            {
                return clef;
            }
        }
    }

    return score::Clef{};
}

[[nodiscard]] bool holdsAnyEvent(const score::Part& part)
{
    for (const score::Measure& measure : part.measures)
    {
        for (const score::Voice& voice : measure.voices)
        {
            if (!voice.events.empty())
            {
                return true;
            }
        }
    }

    return false;
}
} // namespace

ScoreLayout
layOutScore(const std::shared_ptr<const score::Score>& score, const LayoutOptions& options)
{
    ScoreLayout layout;

    if (score == nullptr)
    {
        return layout;
    }

    layout.scoreTitle = score->metadata.workTitle.empty() ? score->metadata.movementTitle
                                                          : score->metadata.workTitle;

    NoticeList notices;

    if (score->parts.empty())
    {
        layout.outcome = LayoutOutcome::nothingToDraw;
        notices.add("This score holds no parts.");
        layout.notices = notices.take();

        return layout;
    }

    const score::Part& part = score->parts.front();

    layout.partName = part.name.empty() ? part.id : part.name;

    if (score->parts.size() > 1)
    {
        notices.add(
            "Only the first part is drawn. Not drawn: " + otherPartNames(score->parts) +
            ". Choosing a part is #33.");
    }

    constexpr int drawnStaff = 1;

    if (part.staffCount > 1)
    {
        notices.add(
            "This part has " + std::to_string(part.staffCount) +
            " staves and only the first is drawn. The braced grand staff is #200.");
    }

    const score::Clef opening = openingClef(part, drawnStaff);

    if (!isPitchedClef(opening))
    {
        layout.outcome = LayoutOutcome::nothingToDraw;
        notices.add(
            "This part is written in a \"" + opening.sign +
            "\" clef, which names no pitch, so its notes have no staff position to be drawn at.");
        layout.notices = notices.take();

        return layout;
    }

    if (part.measures.empty() || !holdsAnyEvent(part))
    {
        layout.outcome = LayoutOutcome::nothingToDraw;
        notices.add("This part holds no notes or rests.");
        layout.notices = notices.take();

        return layout;
    }

    if (!keySignatureIsConventional(opening))
    {
        notices.add("This part's clef sits on an unusual line, and its key signature is placed by "
                    "shifting the conventional positions rather than from a checked table.");
    }

    std::size_t skippedOnOtherStaves = 0;

    const std::vector<MeasurePlan> plans =
        planPart(part, drawnStaff, options, notices, layout.lyricVerseCount, skippedOnOtherStaves);

    if (skippedOnOtherStaves > 0)
    {
        notices.add(
            "Not drawn: " + pluralised(skippedOnOtherStaves, "event") +
            " written on another staff of this part.");
    }

    // Pack measures into systems. At least one measure per system, however
    // wide it is: a bar that does not fit anywhere has to go somewhere, and
    // overflowing is more honest than dropping it.
    std::vector<std::vector<std::size_t>> packed;
    double cursor = 0.0;

    for (std::size_t index = 0; index < plans.size(); ++index)
    {
        const MeasurePlan& plan = plans.at(index);

        const bool wouldStartSystem = packed.empty() || packed.back().empty();

        const double width = headerWidthFor(plan, wouldStartSystem, options) + plan.contentWidth;

        if (!wouldStartSystem && cursor + width > options.availableWidth)
        {
            packed.emplace_back();
            cursor = headerWidthFor(plan, true, options) + plan.contentWidth;
        }
        else
        {
            if (packed.empty())
            {
                packed.emplace_back();
            }

            cursor += width;
        }

        packed.back().push_back(index);
    }

    const double systemHeight =
        options.spaceAboveStaff + staffHeightInSpaces +
        (static_cast<double>(layout.lyricVerseCount) * options.lyricRowHeight) +
        options.spaceBelowStaff;

    NotePlacementMap placements;

    layout.systems.reserve(packed.size());

    for (std::size_t systemIndex = 0; systemIndex < packed.size(); ++systemIndex)
    {
        const std::vector<std::size_t>& measures = packed.at(systemIndex);

        System system;
        system.topOfStaff =
            (static_cast<double>(systemIndex) * systemHeight) + options.spaceAboveStaff;
        system.firstMeasureIndex = plans.at(measures.front()).index;
        system.lastMeasureIndex = plans.at(measures.back()).index;

        double x = 0.0;

        for (std::size_t slot = 0; slot < measures.size(); ++slot)
        {
            const std::size_t planIndex = measures.at(slot);

            x = emitMeasure(
                system, systemIndex, plans.at(planIndex), slot == 0, planIndex + 1 == plans.size(),
                options, placements, x);
        }

        system.width = x;

        layout.systems.push_back(std::move(system));
    }

    // Ties, once every notehead has a system and an x. A tie whose two ends
    // landed on different systems is reported rather than drawn as a curve
    // running the wrong way across the page.
    std::size_t tiesAcrossSystems = 0;

    for (const MeasurePlan& plan : plans)
    {
        for (const PlacedEvent& placed : plan.events)
        {
            for (const PlacedNote& note : placed.notes)
            {
                if (!note.startsTie)
                {
                    continue;
                }

                const score::Note& modelNote = placed.event->notes.at(note.ref.noteIndex);

                if (!modelNote.tiedTo.has_value())
                {
                    continue;
                }

                const auto start = placements.find(note.ref);
                const auto end = placements.find(*modelNote.tiedTo);

                if (start == placements.end() || end == placements.end())
                {
                    continue;
                }

                if (start->second.systemIndex != end->second.systemIndex)
                {
                    ++tiesAcrossSystems;

                    continue;
                }

                layout.systems.at(start->second.systemIndex)
                    .ties.push_back(TieGlyph{
                        start->second.x, end->second.x, start->second.position,
                        !start->second.stemUp});
            }
        }
    }

    if (tiesAcrossSystems > 0)
    {
        notices.add(
            "Not drawn: " + pluralised(tiesAcrossSystems, "tie") +
            " whose two notes fall on different lines of the layout.");
    }

    const bool anyBeam = std::any_of(
        layout.systems.begin(), layout.systems.end(),
        [](const System& system) { return !system.beams.empty(); });

    if (anyBeam)
    {
        notices.add(
            "Beam groups are chosen from the time signature. MusicXML's own <beam> elements are "
            "not read into the model, so the grouping a file was written with is not "
            "necessarily the grouping drawn.");
    }

    layout.outcome = LayoutOutcome::laidOut;
    layout.totalHeight = static_cast<double>(layout.systems.size()) * systemHeight;
    layout.notices = notices.take();

    return layout;
}
} // namespace engraving
