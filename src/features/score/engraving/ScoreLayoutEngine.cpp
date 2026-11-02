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

    // The staff this plan draws, as MusicXML numbers it, and the measure it was
    // planned from. Kept because spacing runs after every staff of a measure
    // has been planned, across all of them at once, and needs both.
    int staff = 1;
    const score::Measure* source = nullptr;

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

// What to call a clef that names no pitch, in words a reader uses. MusicXML's
// <sign> values -- "percussion", "TAB", "none" -- are file vocabulary, and
// quoting them at someone who only wanted to read the music tells them nothing.
[[nodiscard]] std::string unpitchedClefDescription(const std::string& sign)
{
    if (sign == "percussion")
    {
        return "a percussion clef";
    }

    if (sign == "TAB")
    {
        return "tablature";
    }

    return "a clef without pitches";
}

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

[[nodiscard]] std::vector<EventInMeasure> eventsOnStaff(const score::Measure& measure, int staff)
{
    std::vector<EventInMeasure> collected;

    for (std::size_t voiceIndex = 0; voiceIndex < measure.voices.size(); ++voiceIndex)
    {
        const score::Voice& voice = measure.voices.at(voiceIndex);

        for (std::size_t eventIndex = 0; eventIndex < voice.events.size(); ++eventIndex)
        {
            const score::ScoreEvent& event = voice.events.at(eventIndex);

            // Not counted as skipped here. With every staff of a part drawn,
            // each staff's pass would count the others' events as missing;
            // what is genuinely not drawn is counted once, by layOutScore,
            // against the staves it actually left out.
            if (event.staff != staff)
            {
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
                notices.add("Beams stop at rests, so notes either side of a rest are drawn with "
                            "flags.");
            }
        }

        std::size_t consecutiveGraceNotes = 0;

        for (const std::size_t index : eventIndices)
        {
            const PlacedEvent& placed = plan.events.at(index);

            consecutiveGraceNotes = placed.event->isGrace ? consecutiveGraceNotes + 1 : 0;

            if (consecutiveGraceNotes > 1)
            {
                notices.add("Grace notes are drawn with flags rather than beams.");

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

// Place one staff's events in one measure vertically: positions and
// accidentals, because the accidental state machine runs in reading order
// across every voice, then stem directions and beam groups.
//
// Horizontal placement is not here. It is shared across every staff of the
// measure, so it runs in `spaceMeasure` once all of them have been planned.
void planMeasure(
    MeasurePlan& plan,
    const score::Measure& measure,
    int staff,
    const LayoutOptions& options,
    NoticeList& notices,
    int& lyricVerseCount)
{
    const std::vector<EventInMeasure> inOrder = eventsOnStaff(measure, staff);

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
                "Some notes are a length no single note symbol shows, and are drawn as "
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
                    "This part switches to a clef without pitches, such as percussion, and the "
                    "notes after the switch are not shown.");

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
        //
        // The *lowest numbered* voice, not the first one met. `voiceNumbersSeen`
        // is filled in reading order -- onset first, voice second -- so the
        // first entry is the voice that happens to start the measure. A staff
        // where voice 2 sounds from beat one and voice 1 enters at beat two
        // with no written rest (a `<forward>` leaves exactly that gap) would
        // otherwise put up stems on voice 2 and down stems on voice 1, which
        // inverts the one cue a reader uses to tell the parts apart.
        const int upperVoice = *std::min_element(voiceNumbersSeen.begin(), voiceNumbersSeen.end());

        for (PlacedEvent& placed : plan.events)
        {
            placed.stemUp = placed.voiceNumber == upperVoice;
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
}

// Place one measure horizontally, across every staff drawn.
//
// Each staff's slots are built exactly as a lone staff's always were -- the
// same loop, so a slot's room comes out the same, including the order-
// dependent way grace notes add room and an accidental takes the larger. Then
// one shared slot is made per onset any staff has, needing the most room any
// staff needs there, and every staff places its events from those.
//
// That sharing is the whole of a grand staff's alignment. Spaced separately,
// each staff would be correct on its own and still disagree with the other
// about where the beats fall. With one staff the union of onsets is that
// staff's onsets and the largest room is its own, so this produces exactly
// what spacing a single staff always produced.
void spaceMeasure(const std::vector<MeasurePlan*>& staves, const LayoutOptions& options)
{
    const score::Measure& measure = *staves.front()->source;

    std::vector<std::vector<Slot>> slotsByStaff;
    slotsByStaff.reserve(staves.size());

    for (const MeasurePlan* plan : staves)
    {
        std::vector<Slot> slots;

        for (std::size_t index = 0; index < plan->events.size(); ++index)
        {
            const PlacedEvent& placed = plan->events.at(index);
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

        slotsByStaff.push_back(std::move(slots));
    }

    // One shared slot per onset, in time order, with the most room any staff
    // needs there: a sharp in the right hand moves the left hand's note at the
    // same onset with it, so the two stay one above the other.
    std::map<score::Tick, double> roomAt;

    for (const std::vector<Slot>& slots : slotsByStaff)
    {
        for (const Slot& slot : slots)
        {
            const auto [entry, inserted] = roomAt.emplace(slot.onset, slot.leftPad);

            if (!inserted)
            {
                entry->second = std::max(entry->second, slot.leftPad);
            }
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

    std::map<score::Tick, double> xAt;
    double cursor = 0.0;

    for (const auto& [onset, leftPad] : roomAt)
    {
        const double proportional = static_cast<double>(onset) * stretchPerTick;

        // Proportional even for the first onset. A measure whose first event
        // does not begin at tick zero -- a staff that opens with a `<forward>`,
        // which the importer carries as a gap rather than as a rest -- would
        // otherwise draw that event hard against the measure start, as if it
        // fell on beat one, and then jump to its proportional place for the
        // next slot. For the usual onset of zero this is `leftPad`, as before.
        const double x =
            xAt.empty() ? proportional + leftPad
                        : std::max(proportional + leftPad, cursor + slotFloor + leftPad);

        xAt.emplace(onset, x);
        cursor = x;
    }

    for (std::size_t staffIndex = 0; staffIndex < staves.size(); ++staffIndex)
    {
        MeasurePlan& plan = *staves.at(staffIndex);

        for (const Slot& slot : slotsByStaff.at(staffIndex))
        {
            const double x = xAt.at(slot.onset);

            for (const std::size_t eventIndex : slot.mainEvents)
            {
                plan.events.at(eventIndex).localX = x;
            }

            // Grace notes sit immediately before the beat they lead into, in
            // the room reserved for them, and take no time of their own. Their
            // count is this staff's own: another staff's grace notes made the
            // shared slot wider, not this staff's grace notes further apart.
            const std::size_t graceCount = slot.graceEvents.size();

            for (std::size_t graceIndex = 0; graceIndex < graceCount; ++graceIndex)
            {
                plan.events.at(slot.graceEvents.at(graceIndex)).localX =
                    x - (static_cast<double>(graceCount - graceIndex) * options.graceNoteWidth);
            }
        }
    }

    // A measure is as wide as its musical length, not as its note count: a bar
    // of four quarters holding one whole rest is still a bar of four quarters.
    score::Tick measureDuration = measure.nominalDuration;

    for (const MeasurePlan* plan : staves)
    {
        measureDuration = std::max(measureDuration, plan->source->nominalDuration);

        for (const PlacedEvent& placed : plan->events)
        {
            measureDuration = std::max(measureDuration, score::endOf(*placed.event));
        }
    }

    const score::Tick lastOnset = roomAt.empty() ? 0 : roomAt.rbegin()->first;
    const double trailing =
        static_cast<double>(std::max<score::Tick>(measureDuration - lastOnset, 0)) * stretchPerTick;

    // One width for the measure on every staff, so the barline that crosses
    // them all lands at the end of each.
    const double contentWidth =
        cursor + std::max(trailing, slotFloor) + options.measureTrailingWidth;

    // Tempo and dynamic markings, placed at the slot they were attached to so
    // that a marking lands under its note.
    //
    // The proportional position is not that place. A slot is pushed right by
    // whatever room its accidentals and grace notes needed, and by
    // `minimumSlotWidth` once the music is dense enough that proportional
    // spacing alone would crowd it -- sixteenth notes are already past that
    // point. Positioning a marking from the raw onset therefore drifts further
    // left of its note with every slot, which in a bar of sixteenths puts it
    // most of a bar away, under a different beat entirely.
    const auto xForOnset = [&xAt, contentWidth](score::Tick onset)
    {
        if (xAt.empty())
        {
            return 0.0;
        }

        // The slot at that onset, or the last one before it: a marking between
        // two notes belongs to the one it follows.
        double x = xAt.begin()->second;

        for (const auto& [slotOnset, slotX] : xAt)
        {
            if (slotOnset > onset)
            {
                break;
            }

            x = slotX;
        }

        return std::min(x, contentWidth);
    };

    for (MeasurePlan* plan : staves)
    {
        // Each staff's own measure: on a grand staff every staff shares one, but
        // each part has its own, with its own directions and its own length.
        const score::Measure& own = *plan->source;

        plan->contentWidth = contentWidth;

        // A measure whose only content is a rest covering it centres that rest,
        // which is the convention and is what makes an empty bar legible.
        if (plan->events.size() == 1 && plan->events.front().isRest &&
            plan->events.front().event->duration >= own.nominalDuration && own.nominalDuration > 0)
        {
            plan->events.front().localX = contentWidth * 0.5;
        }

        for (const score::Direction& direction : own.directions)
        {
            if (direction.staff != plan->staff)
            {
                continue;
            }

            const double x = xForOnset(direction.onset);

            if (direction.kind == score::DirectionKind::dynamic)
            {
                if (!direction.text.empty())
                {
                    plan->texts.push_back(TextGlyph{x, direction.text, TextRole::dynamic});
                }

                continue;
            }

            std::string label = direction.text;

            if (direction.beatsPerMinute.has_value())
            {
                // Whole beats per minute: a tempo mark reads as a number, and
                // the model keeps the exact value for whatever plays it.
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
                plan->texts.push_back(TextGlyph{x, label, TextRole::tempo});
            }
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
    int staff = 0;
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

// Where one staff's music would start in a measure beginning at `startX`, after
// whatever header it draws there. The same additions in the same order as the
// header walk in `emitMeasure`, so that for a single staff the shared start --
// the largest of the staves' -- is bit for bit the value it always was.
[[nodiscard]] double contentStartFor(
    const MeasurePlan& plan,
    bool startsSystem,
    double startX,
    const LayoutOptions& options)
{
    double cursor = startX;

    if (plan.repeatStart)
    {
        cursor += repeatBarlineWidth;
    }

    if (startsSystem || plan.clefChangesHere)
    {
        cursor += options.clefWidth;
    }

    if (startsSystem || plan.keyChangesHere)
    {
        // Added once per accidental rather than multiplied, because that is how
        // the header walk advances, and a product can differ from a repeated sum
        // in the last bit.
        const std::size_t count = keySignatureAccidentals(plan.clef, plan.key).size();

        for (std::size_t accidental = 0; accidental < count; ++accidental)
        {
            cursor += options.keyAccidentalWidth;
        }
    }

    if (plan.timeChangesHere)
    {
        cursor += options.timeSignatureWidth;
    }

    if (cursor > startX)
    {
        cursor += headerGap;
    }

    return cursor + contentLeadIn;
}

// Lay one staff's glyphs for one measure into a system, starting at `startX`,
// and return the x its barline sits at.
//
// `contentStart` is shared by every staff of the measure: a staff whose header
// is narrower -- the upper staff, when only the lower changes clef -- starts
// its music at the same x as the others, so beats stay aligned. The barlines,
// the opening repeat barline and the measure number belong to the whole
// system, and are emitted only with the first staff.
double emitMeasure(
    System& system,
    std::size_t systemIndex,
    const MeasurePlan& plan,
    bool startsSystem,
    bool isLastMeasure,
    const LayoutOptions& options,
    NotePlacementMap& placements,
    double startX,
    double contentStart,
    int staffIndex)
{
    const bool firstStaff = staffIndex == 0;
    double cursor = startX;

    if (plan.repeatStart)
    {
        if (firstStaff)
        {
            system.barlines.push_back(BarlineGlyph{cursor, BarlineKind::repeatStart});
        }

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

    // The last thing in the header, so nothing after it reads the cursor: where
    // the music starts is `contentStart`, shared by every staff and worked out
    // by `contentStartFor` from these same widths.
    if (plan.timeChangesHere)
    {
        system.timeSignatures.push_back(
            TimeSignatureGlyph{cursor, plan.time.beats, plan.time.beatType});
    }

    if (startsSystem && firstStaff && !plan.printedNumber.empty())
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
                NotePlacement{
                    systemIndex, x + note.xOffset, note.position, placed.stemUp, staffIndex});
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

    if (firstStaff)
    {
        system.barlines.push_back(BarlineGlyph{
            barlineX, plan.repeatEnd ? BarlineKind::repeatEnd
                                     : (isLastMeasure ? BarlineKind::final : BarlineKind::single)});
    }

    return barlineX;
}

// How many of each staff-placed glyph a system held before one staff of a
// measure was emitted, so that what the staff added can be tagged with it.
// Barlines are not here: they belong to the system, not to a staff.
struct GlyphCounts
{
    std::size_t clefs = 0;
    std::size_t keyAccidentals = 0;
    std::size_t timeSignatures = 0;
    std::size_t noteheads = 0;
    std::size_t stems = 0;
    std::size_t beams = 0;
    std::size_t rests = 0;
    std::size_t lyrics = 0;
    std::size_t texts = 0;
};

[[nodiscard]] GlyphCounts glyphCountsOf(const System& system)
{
    return GlyphCounts{
        system.clefs.size(),     system.keyAccidentals.size(), system.timeSignatures.size(),
        system.noteheads.size(), system.stems.size(),          system.beams.size(),
        system.rests.size(),     system.lyrics.size(),         system.texts.size()};
}

template <typename Glyphs> void assignStaffFrom(Glyphs& glyphs, std::size_t from, int staff)
{
    for (std::size_t index = from; index < glyphs.size(); ++index)
    {
        glyphs.at(index).staff = staff;
    }
}

// Tag everything one staff just emitted with that staff. Done after the fact,
// rather than threaded through every glyph `emitMeasure` builds, because beam
// stems are found again by their index in `stems` -- which tagging afterwards
// leaves exactly where it was.
void assignStaff(System& system, const GlyphCounts& before, int staff)
{
    if (staff == 0)
    {
        return;
    }

    assignStaffFrom(system.clefs, before.clefs, staff);
    assignStaffFrom(system.keyAccidentals, before.keyAccidentals, staff);
    assignStaffFrom(system.timeSignatures, before.timeSignatures, staff);
    assignStaffFrom(system.noteheads, before.noteheads, staff);
    assignStaffFrom(system.stems, before.stems, staff);
    assignStaffFrom(system.beams, before.beams, staff);
    assignStaffFrom(system.rests, before.rests, staff);
    assignStaffFrom(system.lyrics, before.lyrics, staff);
    assignStaffFrom(system.texts, before.texts, staff);
}

// A tempo mark is the whole score's, and is engraved once, above the top staff.
// Exporters write it into every part, so with several parts drawn it would
// otherwise appear over each of them. Marks from below the first part are moved
// to the top staff, and dropped where the top staff already has the same mark at
// the same place. The first part's own staves are left alone, so a part drawn
// on its own is exactly as it was.
void liftTempoMarksToTop(System& system, int lastStaffOfFirstPart)
{
    std::vector<TextGlyph> kept;
    kept.reserve(system.texts.size());

    for (const TextGlyph& text : system.texts)
    {
        if (text.role != TextRole::tempo || text.staff <= lastStaffOfFirstPart)
        {
            kept.push_back(text);
        }
    }

    for (const TextGlyph& text : system.texts)
    {
        if (text.role != TextRole::tempo || text.staff <= lastStaffOfFirstPart)
        {
            continue;
        }

        const bool alreadyShown = std::any_of(
            kept.begin(), kept.end(),
            [&text](const TextGlyph& other)
            {
                return other.role == TextRole::tempo && other.text == text.text &&
                       std::abs(other.x - text.x) < 1.0e-9;
            });

        if (!alreadyShown)
        {
            TextGlyph lifted = text;
            lifted.staff = 0;
            kept.push_back(lifted);
        }
    }

    system.texts = std::move(kept);
}

// Walk the part once, resolving the clef, key and time in force at each
// measure and placing that measure's events.
[[nodiscard]] std::vector<MeasurePlan> planPart(
    const score::Part& part,
    int staff,
    const LayoutOptions& options,
    NoticeList& notices,
    int& lyricVerseCount)
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
        plan.staff = staff;
        plan.source = &measure;
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

        // Copied into a local once checked. Dereferencing the member twice with
        // calls between reads, to a dataflow checker, as a second unchecked
        // access -- clang-tidy 21 reports it, 18 does not.
        if (const std::optional<score::KeySignature> declaredKey = measure.attributes.key)
        {
            const bool cancelling = keySignatureAccidentalCount(*declaredKey) == 0 &&
                                    keySignatureAccidentalCount(key) > 0;

            key = *declaredKey;
            plan.keyChangesHere = true;

            if (cancelling)
            {
                notices.add(
                    "A change to a key with no sharps or flats is shown without the naturals "
                    "that cancel the old key.");
            }
        }

        if (const std::optional<score::TimeSignature> declaredTime = measure.attributes.time)
        {
            time = *declaredTime;
            plan.timeChangesHere = true;
        }

        plan.clef = clef;
        plan.key = key;
        plan.time = time;

        if (!measure.repeats.endingNumbers.empty())
        {
            notices.add("First and second endings are not marked yet.");
        }

        if (!measure.repeats.jumps.empty())
        {
            notices.add("Navigation marks such as D.C., D.S., Fine and Coda are not shown yet.");
        }

        planMeasure(plan, measure, staff, options, notices, lyricVerseCount);

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
        notices.add("This score has no parts to show.");
        layout.notices = notices.take();

        return layout;
    }

    // Every part is drawn, each on its own staves, stacked top to bottom in
    // score order. A score of one part reads exactly as it did when only the
    // first part was drawn: every sentence below names "this part" then, and
    // the part's own name only when there are several to tell apart.
    const bool severalParts = score->parts.size() > 1;

    const auto nameOf = [](const score::Part& part)
    { return part.name.empty() ? part.id : part.name; };

    struct DrawnPart
    {
        const score::Part* part = nullptr;
        std::vector<int> staves;
    };

    std::vector<DrawnPart> drawnParts;

    for (const score::Part& part : score->parts)
    {
        const std::string subject = severalParts ? nameOf(part) : "This part";
        const std::string object = severalParts ? nameOf(part) : "this part";

        // Every staff of the part that names pitches. A staff in a clef without
        // pitches -- percussion inside an otherwise pitched part -- is left out
        // and said so, and the part is left out only when no staff can be drawn.
        const int staffCount = std::max(1, part.staffCount);
        std::vector<int> drawnStaves;
        std::vector<std::string> omittedStaves;

        for (int staff = 1; staff <= staffCount; ++staff)
        {
            const score::Clef opening = openingClef(part, staff);

            if (isPitchedClef(opening))
            {
                drawnStaves.push_back(staff);
            }
            else
            {
                omittedStaves.push_back(unpitchedClefDescription(opening.sign));
            }
        }

        if (drawnStaves.empty())
        {
            notices.add(
                subject + " is written in " + unpitchedClefDescription(openingClef(part, 1).sign) +
                ", which has no pitches to place on a staff, so it " +
                (severalParts ? "is not shown." : "cannot be shown."));

            continue;
        }

        for (const std::string& description : omittedStaves)
        {
            std::string notice = "One staff of ";
            notice += object;
            notice += " is written in ";
            notice += description;
            notice += ", which has no pitches to place on a staff, so it is not shown.";
            notices.add(std::move(notice));
        }

        if (part.measures.empty() || !holdsAnyEvent(part))
        {
            notices.add(subject + " has no notes or rests to show.");

            continue;
        }

        drawnParts.push_back(DrawnPart{&part, std::move(drawnStaves)});
    }

    if (drawnParts.empty())
    {
        layout.outcome = LayoutOutcome::nothingToDraw;
        layout.notices = notices.take();

        return layout;
    }

    for (const DrawnPart& drawn : drawnParts)
    {
        if (!layout.partName.empty())
        {
            layout.partName += ", ";
        }

        layout.partName += nameOf(*drawn.part);
    }

    const bool anyUncommonClef = std::any_of(
        drawnParts.begin(), drawnParts.end(),
        [](const DrawnPart& drawn)
        {
            return std::any_of(
                drawn.staves.begin(), drawn.staves.end(), [&drawn](int staff)
                { return !keySignatureIsConventional(openingClef(*drawn.part, staff)); });
        });

    if (anyUncommonClef)
    {
        notices.add(
            severalParts ? "A clef in this score sits on an uncommon line, so compare its key "
                           "signature with the printed music."
                         : "This part's clef sits on an uncommon line, so compare its key "
                           "signature with the printed music.");
    }

    // Parts are written measure for measure, so they almost always agree on
    // how many there are. Where one runs longer, the bars only it has cannot be
    // lined up with anything, and are reported rather than drawn ragged.
    std::size_t measureCount = drawnParts.front().part->measures.size();
    std::size_t longest = measureCount;

    for (const DrawnPart& drawn : drawnParts)
    {
        measureCount = std::min(measureCount, drawn.part->measures.size());
        longest = std::max(longest, drawn.part->measures.size());
    }

    if (longest > measureCount)
    {
        notices.add(
            "Not shown: " + pluralised(longest - measureCount, "bar") +
            " at the end that only some parts have.");
    }

    // Each staff planned vertically on its own, then every measure spaced across
    // all of them at once -- see spaceMeasure for why that order is the point.
    // `partOfStaff` says which drawn part each planned staff belongs to.
    std::vector<std::vector<MeasurePlan>> plansByStaff;
    std::vector<std::size_t> partOfStaff;
    std::vector<int> partVerses(drawnParts.size(), 0);

    for (std::size_t partIndex = 0; partIndex < drawnParts.size(); ++partIndex)
    {
        const DrawnPart& drawn = drawnParts.at(partIndex);

        for (const int staff : drawn.staves)
        {
            plansByStaff.push_back(
                planPart(*drawn.part, staff, options, notices, partVerses.at(partIndex)));
            plansByStaff.back().erase(
                plansByStaff.back().begin() + static_cast<std::ptrdiff_t>(measureCount),
                plansByStaff.back().end());
            partOfStaff.push_back(partIndex);
        }

        layout.lyricVerseCount = std::max(layout.lyricVerseCount, partVerses.at(partIndex));
    }

    for (std::size_t measure = 0; measure < measureCount; ++measure)
    {
        std::vector<MeasurePlan*> column;
        column.reserve(plansByStaff.size());

        for (std::vector<MeasurePlan>& plans : plansByStaff)
        {
            column.push_back(&plans.at(measure));
        }

        spaceMeasure(column, options);
    }

    // What is genuinely not drawn: events on a staff left out, or on a staff
    // number the part does not declare. Counted once, here, against the staves
    // actually drawn, rather than by each staff's pass counting the others'.
    std::size_t notDrawn = 0;

    for (const DrawnPart& drawn : drawnParts)
    {
        for (const score::Measure& measure : drawn.part->measures)
        {
            for (const score::Voice& voice : measure.voices)
            {
                for (const score::ScoreEvent& event : voice.events)
                {
                    if (std::find(drawn.staves.begin(), drawn.staves.end(), event.staff) ==
                        drawn.staves.end())
                    {
                        ++notDrawn;
                    }
                }
            }
        }
    }

    if (notDrawn > 0)
    {
        notices.add(
            "Not shown: " + std::to_string(notDrawn) +
            (notDrawn == 1 ? " note or rest" : " notes and rests") +
            " on a staff this view does not show.");
    }

    // Which staves each part spans, for the painter's braces and barlines, and
    // the lyric rows below each staff: a part's lyrics hang below its own
    // staves and push down only the part beneath it.
    std::vector<StaffGroup> staffGroups;
    std::vector<int> staffVerses;

    for (std::size_t staff = 0; staff < partOfStaff.size(); ++staff)
    {
        const std::size_t partIndex = partOfStaff.at(staff);
        const int index = static_cast<int>(staff);

        if (staff == 0 || partOfStaff.at(staff - 1) != partIndex)
        {
            staffGroups.push_back(StaffGroup{index, index});
        }

        staffGroups.back().lastStaff = index;
        staffVerses.push_back(partVerses.at(partIndex));
    }

    // A braced system reserves room at its start for the brace and the barline
    // that joins the staves. Nothing without a brace, so a single staff -- and a
    // choir of single-staff parts -- keeps its x unchanged.
    const bool braced = std::any_of(
        staffGroups.begin(), staffGroups.end(),
        [](const StaffGroup& group) { return group.lastStaff > group.firstStaff; });
    const double braceRoom = braced ? options.braceWidth : 0.0;

    // A measure's header is as wide as its widest staff's: when only the lower
    // staff changes clef, the upper still has to start its music where the
    // lower does.
    const auto headerWidthAt = [&plansByStaff, &options](std::size_t measure, bool startsSystem)
    {
        double widest = headerWidthFor(plansByStaff.front().at(measure), startsSystem, options);

        for (std::size_t staff = 1; staff < plansByStaff.size(); ++staff)
        {
            widest = std::max(
                widest, headerWidthFor(plansByStaff.at(staff).at(measure), startsSystem, options));
        }

        return widest;
    };

    // Pack measures into systems. At least one measure per system, however
    // wide it is: a bar that does not fit anywhere has to go somewhere, and
    // overflowing is more honest than dropping it.
    std::vector<std::vector<std::size_t>> packed;
    double cursor = 0.0;
    const double available = options.availableWidth - braceRoom;

    for (std::size_t index = 0; index < measureCount; ++index)
    {
        // The same on every staff: spaceMeasure gives a measure one width.
        const double contentWidth = plansByStaff.front().at(index).contentWidth;

        const bool wouldStartSystem = packed.empty() || packed.back().empty();

        const double width = headerWidthAt(index, wouldStartSystem) + contentWidth;

        if (!wouldStartSystem && cursor + width > available)
        {
            packed.emplace_back();
            cursor = headerWidthAt(index, true) + contentWidth;
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

    // Each staff after the first starts below the previous one's bottom line,
    // past the gap between them and the upper staff's lyric rows. A piano
    // part's dynamics hang below the right hand, so they fall in that gap.
    const auto lyricRowsBelow = [&staffVerses, &options](std::size_t staff)
    { return static_cast<double>(staffVerses.at(staff)) * options.lyricRowHeight; };

    std::vector<double> staffOffsets{0.0};

    for (std::size_t staff = 1; staff < plansByStaff.size(); ++staff)
    {
        staffOffsets.push_back(
            staffOffsets.back() + staffHeightInSpaces + options.interStaffGap +
            lyricRowsBelow(staff - 1));
    }

    const double systemHeight =
        options.spaceAboveStaff + staffOffsets.back() + staffHeightInSpaces +
        lyricRowsBelow(plansByStaff.size() - 1) + options.spaceBelowStaff;

    // One map per part: a NoteRef addresses a note within its own part, so two
    // parts' first notes have the same ref.
    std::vector<NotePlacementMap> placements(drawnParts.size());

    layout.systems.reserve(packed.size());

    for (std::size_t systemIndex = 0; systemIndex < packed.size(); ++systemIndex)
    {
        const std::vector<std::size_t>& measures = packed.at(systemIndex);

        System system;
        system.topOfStaff =
            (static_cast<double>(systemIndex) * systemHeight) + options.spaceAboveStaff;
        system.firstMeasureIndex = plansByStaff.front().at(measures.front()).index;
        system.lastMeasureIndex = plansByStaff.front().at(measures.back()).index;
        system.staffOffsets = staffOffsets;
        system.staffGroups = staffGroups;
        system.staffLyricVerses = staffVerses;
        system.staffStartX = braceRoom;

        double x = braceRoom;

        for (std::size_t slot = 0; slot < measures.size(); ++slot)
        {
            const std::size_t measure = measures.at(slot);
            const bool startsSystem = slot == 0;
            const bool isLastMeasure = measure + 1 == measureCount;

            // Every staff's music starts where the latest-starting staff's does.
            // For a single staff this is its own start, exactly as before.
            double contentStart =
                contentStartFor(plansByStaff.front().at(measure), startsSystem, x, options);

            for (std::size_t staff = 1; staff < plansByStaff.size(); ++staff)
            {
                contentStart = std::max(
                    contentStart,
                    contentStartFor(plansByStaff.at(staff).at(measure), startsSystem, x, options));
            }

            double barlineX = x;

            for (std::size_t staff = 0; staff < plansByStaff.size(); ++staff)
            {
                const GlyphCounts before = glyphCountsOf(system);

                barlineX = emitMeasure(
                    system, systemIndex, plansByStaff.at(staff).at(measure), startsSystem,
                    isLastMeasure, options, placements.at(partOfStaff.at(staff)), x, contentStart,
                    static_cast<int>(staff));

                assignStaff(system, before, static_cast<int>(staff));
            }

            x = barlineX;
        }

        system.width = x;

        liftTempoMarksToTop(system, staffGroups.front().lastStaff);

        layout.systems.push_back(std::move(system));
    }

    // Ties, once every notehead has a system and an x. A tie whose two ends
    // landed on different systems is reported rather than drawn as a curve
    // running the wrong way across the page. Each is drawn on the staff of the
    // note it leaves.
    std::size_t tiesAcrossSystems = 0;

    for (std::size_t staff = 0; staff < plansByStaff.size(); ++staff)
    {
        const NotePlacementMap& partPlacements = placements.at(partOfStaff.at(staff));

        for (const MeasurePlan& plan : plansByStaff.at(staff))
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

                    const auto start = partPlacements.find(note.ref);
                    const auto end = partPlacements.find(*modelNote.tiedTo);

                    if (start == partPlacements.end() || end == partPlacements.end())
                    {
                        continue;
                    }

                    if (start->second.systemIndex != end->second.systemIndex)
                    {
                        ++tiesAcrossSystems;

                        continue;
                    }

                    TieGlyph tie{
                        start->second.x, end->second.x, start->second.position,
                        !start->second.stemUp};
                    tie.staff = start->second.staff;

                    layout.systems.at(start->second.systemIndex).ties.push_back(tie);
                }
            }
        }
    }

    if (tiesAcrossSystems > 0)
    {
        notices.add(
            "Not shown: " + pluralised(tiesAcrossSystems, "tie") +
            " that run from one line of music onto the next.");
    }

    const bool anyBeam = std::any_of(
        layout.systems.begin(), layout.systems.end(),
        [](const System& system) { return !system.beams.empty(); });

    if (anyBeam)
    {
        notices.add(
            "Beams are grouped by the time signature, so they may not match how the original "
            "file grouped them.");
    }

    layout.outcome = LayoutOutcome::laidOut;
    layout.totalHeight = static_cast<double>(layout.systems.size()) * systemHeight;
    layout.notices = notices.take();

    return layout;
}
} // namespace engraving
