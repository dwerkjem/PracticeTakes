#include "ScoreLayoutEngine.h"

#include "AccidentalStacking.h"
#include "BeamGeometry.h"
#include "BeamGrouping.h"
#include "KeySignatureLayout.h"
#include "MeasureAccidentals.h"
#include "PartNames.h"

#include <algorithm>
#include <cctype>
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

// How full a system has to be, of the width it has, for a break to move back
// one bar to a repeat barline or the end of an ending (design.md decision 13).
constexpr double structuralBreakMinimumFill = 0.75;

// How far short of the system's end the first half of a split tie stops, so it
// does not run into the barline.
constexpr double tieHalfEndInset = 0.4;

// How far after the next system's header the second half starts, so it does
// not touch the last sign of the key signature.
constexpr double tieHalfStartGap = 0.3;

// One note of one event, already positioned vertically.
struct PlacedNote
{
    score::NoteRef ref;
    StaffSteps position = 0;
    AccidentalKind accidental = AccidentalKind::none;

    // Moved across the stem when the note sits a second away from its
    // neighbour in the same chord, which is the one case two noteheads would
    // otherwise overlap: right of an up stem, left of a down one.
    double xOffset = 0.0;

    bool startsTie = false;

    // Where the accidental's left edge goes, relative to the event's x. Set by
    // `spaceMeasure` once the chord's signs have been stacked.
    double accidentalOffset = 0.0;
};

struct PlacedEvent
{
    const score::ScoreEvent* event = nullptr;

    int voiceNumber = 1;

    // Relative to the start of the measure's content, in staff spaces.
    double localX = 0.0;

    // The x of the onset this event hangs from: its own for a main note, the
    // note it leads into for a grace note. Justification stretches the gaps
    // between onsets and moves each event with its onset, so a grace note and
    // an accidental keep their distance from the note they belong to.
    double anchorX = 0.0;

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

    // Tempo marks lent to this staff by a part the reader hid. A tempo is the
    // whole score's, and an exporter often writes it into one part only, so
    // hiding that part must not hide the tempo with it (score-part-selection).
    std::vector<score::Direction> borrowedTempos;

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

// Horizontal offsets for the notes of one chord (design.md decision 4). Two
// notes a second apart cannot share an x, so one of them crosses the stem.
//
// Which one depends on the stem. With the stem up the noteheads hang to its
// left, and the walk starts at the bottom note, the one the stem attaches to:
// the *upper* note of a second moves right, across the stem. With the stem
// down they hang to its right, the walk starts at the top note, and the *lower*
// note moves left. Either way the note the stem attaches to stays put, which is
// what keeps the stem on the chord's own side.
//
// Run after stem direction is resolved, which is the whole reason it is not
// done as the notes are read.
void offsetSeconds(std::vector<PlacedNote>& notes, double noteheadWidth, bool stemUp)
{
    if (notes.size() < 2)
    {
        return;
    }

    std::vector<PlacedNote*> fromStemEnd;
    fromStemEnd.reserve(notes.size());

    for (PlacedNote& note : notes)
    {
        note.xOffset = 0.0;
        fromStemEnd.push_back(&note);
    }

    // A larger position is lower on the staff. Up stems walk from the bottom,
    // down stems from the top.
    std::stable_sort(
        fromStemEnd.begin(), fromStemEnd.end(),
        [stemUp](const PlacedNote* lhs, const PlacedNote* rhs)
        { return stemUp ? lhs->position > rhs->position : lhs->position < rhs->position; });

    const double shift = stemUp ? noteheadWidth : -noteheadWidth;

    for (std::size_t index = 1; index < fromStemEnd.size(); ++index)
    {
        const PlacedNote& previous = *fromStemEnd.at(index - 1);
        PlacedNote& note = *fromStemEnd.at(index);

        // A second is one half space. Only shift when the previous note is not
        // itself already shifted, so a cluster alternates rather than marching
        // off to one side.
        if (std::abs(previous.position - note.position) == 1 && previous.xOffset == 0.0)
        {
            note.xOffset = shift;
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

    // How far the main chord reaches left of its own x: a notehead pushed left
    // of a down stem, then its accidentals. The grace notes go left of that.
    double mainReach = 0.0;

    // Parallel to `graceEvents`: how far each grace note's own accidentals
    // reach left of it.
    std::vector<double> graceReach;
};

// Stack the accidentals of these events, which share one x, and record where
// each sign went on its note. Returns how far the result reaches left of that
// x: the chord's leftmost notehead, then the stack's width.
//
// Every event at one onset on one staff is one chord for this purpose, whatever
// voice it is in. Two voices' heads share the slot's x, so their signs would
// collide exactly as one chord's would.
double placeAccidentals(
    MeasurePlan& plan,
    const std::vector<std::size_t>& eventIndices,
    const LayoutOptions& options,
    double scale)
{
    double chordLeft = 0.0;
    std::vector<AccidentalToStack> signs;
    std::vector<PlacedNote*> owners;

    for (const std::size_t eventIndex : eventIndices)
    {
        for (PlacedNote& note : plan.events.at(eventIndex).notes)
        {
            chordLeft = std::min(chordLeft, note.xOffset);
            signs.push_back(AccidentalToStack{note.position, note.accidental});
            owners.push_back(&note);
        }
    }

    const AccidentalStack stack = stackAccidentals(signs, options, scale);

    for (std::size_t index = 0; index < owners.size(); ++index)
    {
        owners.at(index)->accidentalOffset = chordLeft + stack.x.at(index);
    }

    return -chordLeft + stack.width;
}

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

    // Last, because which note of a second moves depends on where the stem
    // ended up -- and a beam group or a second voice can overrule a chord's
    // own direction.
    for (PlacedEvent& placed : plan.events)
    {
        offsetSeconds(placed.notes, options.noteheadWidth, placed.stemUp);
    }
}

// Place one measure horizontally, across every staff drawn.
//
// Each staff's slots are built exactly as a lone staff's always were -- the
// same loop, so a slot's room comes out the same: what its chord's stacked
// accidentals and any grace notes before it use. Then one shared slot is made
// per onset any staff has, needing the most room any staff needs there, and
// every staff places its events from those.
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

    for (MeasurePlan* plan : staves)
    {
        std::vector<Slot> slots;

        for (std::size_t index = 0; index < plan->events.size(); ++index)
        {
            const PlacedEvent& placed = plan->events.at(index);
            const score::Tick onset = placed.event->onset;

            if (slots.empty() || slots.back().onset != onset)
            {
                slots.push_back(Slot{onset, 0.0, 0.0, {}, {}, 0.0, {}});
            }

            Slot& slot = slots.back();

            if (placed.event->isGrace)
            {
                slot.graceEvents.push_back(index);
            }
            else
            {
                slot.mainEvents.push_back(index);
            }
        }

        // The room a slot needs is what it holds, right to left: the main
        // chord's reach -- a head pushed left of its stem, then its stacked
        // accidentals -- and then each grace note with its own. With no
        // accidentals and no seconds this is the grace notes alone, as it
        // always was.
        for (Slot& slot : slots)
        {
            slot.mainReach = placeAccidentals(*plan, slot.mainEvents, options, 1.0);
            slot.leftPad = slot.mainReach;

            for (const std::size_t graceIndex : slot.graceEvents)
            {
                const double reach = placeAccidentals(*plan, {graceIndex}, options, graceNoteScale);
                slot.graceReach.push_back(reach);
                slot.leftPad += options.graceNoteWidth + reach;
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
                plan.events.at(eventIndex).anchorX = x;
            }

            // Grace notes sit immediately before the beat they lead into, left
            // of anything the main chord reaches out with, and take no time of
            // their own. Their room is this staff's own: another staff's grace
            // notes made the shared slot wider, not this staff's grace notes
            // further apart. Placed right to left, so each one's accidentals
            // push the grace notes before it out rather than into it.
            double graceCursor = x - slot.mainReach;

            for (std::size_t graceIndex = slot.graceEvents.size(); graceIndex-- > 0;)
            {
                const double graceX = graceCursor - options.graceNoteWidth;
                plan.events.at(slot.graceEvents.at(graceIndex)).localX = graceX;
                plan.events.at(slot.graceEvents.at(graceIndex)).anchorX = x;
                graceCursor = graceX - slot.graceReach.at(graceIndex);
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
            plan->events.front().anchorX = contentWidth * 0.5;
        }

        std::vector<score::Direction> directions = own.directions;
        directions.insert(
            directions.end(), plan->borrowedTempos.begin(), plan->borrowedTempos.end());

        for (const score::Direction& direction : directions)
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

// Whether one staff's measure is a bar of rest a multi-measure rest may absorb
// (design.md decision 12): a whole-bar rest and nothing else, with nothing a
// reader needs to see on it -- no clef, key or time change, no repeat, ending
// or jump, and no direction. Any of those is drawn on its own bar, and so
// breaks the run, because condensing it would hide it.
[[nodiscard]] bool isEmptyBar(const MeasurePlan& plan)
{
    const score::Measure& measure = *plan.source;

    if (plan.clefChangesHere || plan.keyChangesHere || plan.timeChangesHere || plan.repeatStart ||
        plan.repeatEnd || !measure.repeats.endingNumbers.empty() ||
        !measure.repeats.jumps.empty() || !measure.directions.empty() ||
        measure.nominalDuration <= 0 || plan.events.empty())
    {
        return false;
    }

    return std::all_of(
        plan.events.begin(), plan.events.end(),
        [&measure](const PlacedEvent& placed)
        {
            return placed.isRest && !placed.event->isGrace &&
                   placed.event->duration >= measure.nominalDuration;
        });
}

// The ledger lines one event needs, shared across its notes (design.md
// decision 7). A note at or beyond a ledger line's position needs that line, so
// each line spans every note that needs it, and no line is drawn twice.
void emitLedgerLines(
    System& system,
    const PlacedEvent& placed,
    double x,
    NoteheadKind head,
    const LayoutOptions& options)
{
    const double scale = placed.event->isGrace ? graceNoteScale : 1.0;
    const double width = noteheadWidthFor(head) * scale;
    const double extension = options.ledgerLineExtension * scale;
    const double besideAccidental = options.ledgerLineExtensionBesideAccidental * scale;

    StaffSteps highest = topLineSteps;
    StaffSteps lowest = bottomLineSteps;

    for (const PlacedNote& note : placed.notes)
    {
        highest = std::min(highest, note.position);
        lowest = std::max(lowest, note.position);
    }

    const auto lineAt = [&](StaffSteps position, bool above)
    {
        LedgerLineGlyph line;
        line.position = position;
        line.isGrace = placed.event->isGrace;

        bool any = false;

        for (const PlacedNote& note : placed.notes)
        {
            if (above ? note.position > position : note.position < position)
            {
                continue;
            }

            const double left =
                x + note.xOffset -
                (note.accidental == AccidentalKind::none ? extension : besideAccidental);
            const double right = x + note.xOffset + width + extension;

            line.leftX = any ? std::min(line.leftX, left) : left;
            line.rightX = any ? std::max(line.rightX, right) : right;
            any = true;
        }

        if (any)
        {
            system.ledgerLines.push_back(line);
        }
    };

    for (StaffSteps position = topLineSteps - 2; position >= highest; position -= 2)
    {
        lineAt(position, true);
    }

    for (StaffSteps position = bottomLineSteps + 2; position <= lowest; position += 2)
    {
        lineAt(position, false);
    }
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
    int staffIndex,
    double stretch,
    int condensedBars)
{
    // A multi-measure rest draws none of its first bar's own events -- a whole
    // rest -- and stands in a bar of its own width.
    static const std::vector<PlacedEvent> noEvents;
    const bool condensed = condensedBars > 1;
    const std::vector<PlacedEvent>& events = condensed ? noEvents : plan.events;
    const double contentWidth = condensed ? options.multiMeasureRestWidth : plan.contentWidth;

    const bool firstStaff = staffIndex == 0;
    double cursor = startX;

    // Where an event goes once the measure is justified: its onset's x
    // stretched, and its own offset from that onset kept as it was.
    const auto xOf = [contentStart, stretch](const PlacedEvent& placed)
    { return contentStart + placed.localX + (placed.anchorX * (stretch - 1.0)); };

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

    for (const PlacedEvent& placed : events)
    {
        const double x = xOf(placed);

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
            NoteheadGlyph glyph{
                note.ref,        x + note.xOffset, note.position,        noteheadFor(value.value),
                note.accidental, value.dots,       placed.event->isGrace};
            glyph.accidentalX = x + note.accidentalOffset;
            system.noteheads.push_back(glyph);

            placements.emplace(
                note.ref,
                NotePlacement{
                    systemIndex, x + note.xOffset, note.position, placed.stemUp, staffIndex});
        }

        emitLedgerLines(system, placed, x, noteheadFor(value.value), options);

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

    for (const PlacedEvent& placed : events)
    {
        for (const score::LyricSyllable& syllable : placed.event->lyrics)
        {
            system.lyrics.push_back(LyricGlyph{
                xOf(placed), syllable.verse, syllable.text,
                syllable.position == score::SyllabicPosition::begin ||
                    syllable.position == score::SyllabicPosition::middle,
                syllable.extend});
        }
    }

    for (const TextGlyph& text : plan.texts)
    {
        system.texts.push_back(TextGlyph{contentStart + (text.x * stretch), text.text, text.role});
    }

    const double barlineX = contentStart + (contentWidth * stretch);

    if (condensed)
    {
        system.multiRests.push_back(MultiRestGlyph{contentStart, barlineX, condensedBars});
    }

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
    std::size_t ledgerLines = 0;
    std::size_t stems = 0;
    std::size_t beams = 0;
    std::size_t rests = 0;
    std::size_t multiRests = 0;
    std::size_t lyrics = 0;
    std::size_t texts = 0;
};

[[nodiscard]] GlyphCounts glyphCountsOf(const System& system)
{
    return GlyphCounts{
        system.clefs.size(),     system.keyAccidentals.size(), system.timeSignatures.size(),
        system.noteheads.size(), system.ledgerLines.size(),    system.stems.size(),
        system.beams.size(),     system.rests.size(),          system.multiRests.size(),
        system.lyrics.size(),    system.texts.size()};
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
    assignStaffFrom(system.ledgerLines, before.ledgerLines, staff);
    assignStaffFrom(system.stems, before.stems, staff);
    assignStaffFrom(system.beams, before.beams, staff);
    assignStaffFrom(system.rests, before.rests, staff);
    assignStaffFrom(system.multiRests, before.multiRests, staff);
    assignStaffFrom(system.lyrics, before.lyrics, staff);
    assignStaffFrom(system.texts, before.texts, staff);
}

// A tempo mark is the whole score's, and is engraved once, above the top staff.
// Exporters write it into every part, so with several parts drawn it would
// otherwise appear over each of them. Marks from below the first part are moved
// to the top staff, and dropped where the top staff already has the same mark at
// the same place. The first part's own staves are left alone, so a part drawn
// on its own is exactly as it was.
// How far the notes on one staff reach, in staff spaces below its top line:
// `top` is negative when something stands above the staff. Starts as the staff
// lines themselves.
struct StaffExtent
{
    double top = 0.0;
    double bottom = staffHeightInSpaces;

    void include(double y)
    {
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }
};

// What text has to clear on each staff of a system: noteheads with their
// ledger lines, accidentals, stems and beams. Clefs, key and time signatures
// stay inside the staff and are not counted, so a system with a clef change does
// not push its lyrics away for nothing.
[[nodiscard]] std::vector<StaffExtent> noteExtentsOf(const System& system)
{
    std::vector<StaffExtent> extents(std::max<std::size_t>(1, system.staffOffsets.size()));

    const auto on = [&extents](int staff) -> StaffExtent&
    {
        const auto index = static_cast<std::size_t>(std::max(0, staff));

        return extents.at(std::min(index, extents.size() - 1));
    };

    // Half a space each side of a notehead's centre, or as far as its
    // accidental reaches, from the same table the stacking uses: a flat's stem
    // rises nearly two spaces and barely drops below the note.
    constexpr double headReach = 0.5;

    for (const NoteheadGlyph& head : system.noteheads)
    {
        const double y = spacesBelowTopLine(head.position);
        const AccidentalMetrics sign = accidentalMetricsFor(head.accidental);
        const double scale = head.isGrace ? graceNoteScale : 1.0;

        on(head.staff).include(y - std::max(headReach, sign.above * scale));
        on(head.staff).include(y + std::max(headReach, sign.below * scale));
    }

    for (const StemGlyph& stem : system.stems)
    {
        on(stem.staff).include(spacesBelowTopLine(stem.attachPosition));
        on(stem.staff).include(stem.tipSpaces);
    }

    for (const BeamGlyph& beam : system.beams)
    {
        // The beam is `thickness` deep away from the noteheads.
        const double away = beam.up ? -beam.thickness : beam.thickness;

        on(beam.staff).include(beam.startY);
        on(beam.staff).include(beam.endY);
        on(beam.staff).include(beam.startY + away);
        on(beam.staff).include(beam.endY + away);
    }

    return extents;
}

// "1." for a first ending, "1. 2." for one shared by both, as written.
[[nodiscard]] std::string voltaLabelFor(const std::vector<int>& numbers)
{
    std::string label;

    for (const int number : numbers)
    {
        if (!label.empty())
        {
            label += ' ';
        }

        label += std::to_string(number) + '.';
    }

    return label;
}

// Where one measure sits on its system, for the ending brackets drawn over it.
struct MeasureSpan
{
    const score::Measure* measure = nullptr;
    double startX = 0.0;
    double endX = 0.0;
};

// Jump marks for one system (design.md decision 9). "Segno" and "Coda", as
// written in any case, are their signs at the measure's start; every other
// string is text at its end. An unrecognised string is drawn rather than
// dropped, because a jump the reader cannot see sends them the wrong way.
void addJumps(System& system, const std::vector<MeasureSpan>& spans)
{
    constexpr double barlineInset = 0.3;

    const auto lowered = [](std::string text)
    {
        std::transform(
            text.begin(), text.end(), text.begin(), [](char character)
            { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });

        return text;
    };

    for (std::size_t index = 0; index < spans.size(); ++index)
    {
        const MeasureSpan& span = spans.at(index);

        for (const std::string& written : span.measure->repeats.jumps)
        {
            JumpGlyph jump;
            jump.text = written;

            const std::string key = lowered(written);

            if (key == "segno" || key == "coda")
            {
                jump.sign = key == "segno" ? JumpSign::segno : JumpSign::coda;
                jump.x = (index == 0 ? std::max(span.startX, system.headerEndX) : span.startX) +
                         barlineInset;
            }
            else
            {
                jump.x = span.endX - barlineInset;
            }

            system.jumps.push_back(jump);
        }
    }
}

// Ending brackets for one system (design.md decision 8). A bracket is a run of
// consecutive measures sharing the same non-empty ending numbers, and it ends at
// a measure that closes on a repeat barline even if the next has the same
// numbers. `before` is the measure just ahead of this system, if any, so a run
// that started there continues here without a second label.
void addVoltas(System& system, const std::vector<MeasureSpan>& spans, const score::Measure* before)
{
    // Inset from the barlines either side, so a hook does not sit on one.
    constexpr double barlineInset = 0.25;

    const auto continues = [](const score::Measure* previous, const score::Measure& next)
    {
        return previous != nullptr && !previous->repeats.repeatEnd &&
               previous->repeats.endingNumbers == next.repeats.endingNumbers;
    };

    std::size_t index = 0;

    while (index < spans.size())
    {
        const score::Measure& first = *spans.at(index).measure;

        if (first.repeats.endingNumbers.empty())
        {
            ++index;
            continue;
        }

        const score::Measure* previous = index == 0 ? before : spans.at(index - 1).measure;
        const bool carriedOver = index == 0 && continues(previous, first);

        std::size_t last = index;

        while (last + 1 < spans.size() && !spans.at(last).measure->repeats.repeatEnd &&
               spans.at(last + 1).measure->repeats.endingNumbers == first.repeats.endingNumbers)
        {
            ++last;
        }

        VoltaGlyph volta;
        volta.startX = (index == 0 ? std::max(spans.at(index).startX, system.headerEndX)
                                   : spans.at(index).startX) +
                       barlineInset;
        volta.endX = spans.at(last).endX - barlineInset;
        volta.label = carriedOver ? std::string{} : voltaLabelFor(first.repeats.endingNumbers);
        volta.hookAtStart = !carriedOver;
        volta.hookAtEnd = spans.at(last).measure->repeats.repeatEnd;
        system.voltas.push_back(volta);

        index = last + 1;
    }
}

// Puts every line of text on its band, clear of the notes, then stacks the
// staves and systems by what they now hold (design decision 6).
//
// One band per kind of text per staff, per system: every syllable of a verse
// shares one line, every dynamic on a staff another, the tempo marks a third.
// One low note moves its whole system's lyric line, not the syllables near it,
// which is what keeps each band straight. A band starts where it always did and
// only ever moves further from the staff, so a system with nothing in the way
// is laid out exactly as before.
//
// That makes systems different heights, so `topOfStaff` accumulates and
// `totalHeight` is the sum, and a staff's distance from the one above it
// grows when the upper staff's text, or the lower staff's high notes, need it.
void placeTextAndStaves(ScoreLayout& layout, const LayoutOptions& options)
{
    double cursor = 0.0;

    for (System& system : layout.systems)
    {
        const std::vector<StaffExtent> notes = noteExtentsOf(system);
        const std::size_t staffCount = notes.size();

        const auto versesOn = [&system, &layout](std::size_t staff)
        {
            return staff < system.staffLyricVerses.size()
                       ? system.staffLyricVerses.at(staff)
                       : layout.lyricVerseCount;
        };

        // Below the staff: lyric rows from the first clear line down, then the
        // dynamics under them. Above: the tempo line, and the measure numbers
        // over it.
        std::vector<double> belowBase(staffCount);
        std::vector<double> aboveBase(staffCount);

        for (std::size_t staff = 0; staff < staffCount; ++staff)
        {
            belowBase.at(staff) = std::max(
                staffHeightInSpaces + options.textGapBelowStaff,
                notes.at(staff).bottom + options.textClearance);
            aboveBase.at(staff) = std::min(
                -options.tempoAboveStaff,
                notes.at(staff).top - options.textClearance - options.textBandHeight);
        }

        const auto staffIndex = [staffCount](int staff)
        { return std::min(static_cast<std::size_t>(std::max(0, staff)), staffCount - 1); };

        // How far each staff's contents reach once its text is placed.
        std::vector<StaffExtent> reach = notes;

        // Ending brackets sit nearest the top staff, one line for the whole
        // system, with their hooks clear of its notes. The top staff's other
        // text goes above them, so a tempo mark or a measure number is never
        // written through a bracket.
        if (!system.voltas.empty())
        {
            const double lineY = std::min(
                -options.voltaAboveStaff,
                notes.front().top - options.textClearance - options.voltaHookLength);

            for (VoltaGlyph& volta : system.voltas)
            {
                volta.y = lineY;
            }

            reach.front().include(lineY);
            aboveBase.front() =
                std::min(aboveBase.front(), lineY - options.textClearance - options.textBandHeight);
        }

        // Jump marks on a row of their own, where the top staff's text row
        // would be, grown to a sign's height; that text then goes above them.
        if (!system.jumps.empty())
        {
            const double rowTop =
                aboveBase.front() - (options.jumpSignHeight - options.textBandHeight);

            for (JumpGlyph& jump : system.jumps)
            {
                jump.y = rowTop;
            }

            reach.front().include(rowTop);
            aboveBase.front() = rowTop - options.textClearance - options.textBandHeight;
        }

        for (LyricGlyph& lyric : system.lyrics)
        {
            const std::size_t staff = staffIndex(lyric.staff);

            lyric.y = belowBase.at(staff) +
                      (static_cast<double>(std::max(1, lyric.verse) - 1) * options.lyricRowHeight);
            reach.at(staff).include(lyric.y + options.textBandHeight);
        }

        for (TextGlyph& text : system.texts)
        {
            const std::size_t staff = staffIndex(text.staff);

            switch (text.role)
            {
            case TextRole::dynamic:
                text.y = belowBase.at(staff) +
                         (static_cast<double>(versesOn(staff)) * options.lyricRowHeight);
                reach.at(staff).include(text.y + options.textBandHeight);
                break;

            case TextRole::tempo:
                text.y = aboveBase.at(staff);
                reach.at(staff).include(text.y);
                break;

            case TextRole::measureNumber:
                text.y = std::min(
                    -options.measureNumberAboveStaff,
                    aboveBase.at(staff) - options.measureNumberAboveTempo);
                reach.at(staff).include(text.y);
                break;
            }
        }

        // Each staff below the last thing on the one above, and the lowest
        // note it has, by at least the clearance -- but never closer than the
        // fixed spacing it always had.
        const auto lyricRows = [&versesOn, &options](std::size_t staff)
        { return static_cast<double>(versesOn(staff)) * options.lyricRowHeight; };

        std::vector<double> offsets{0.0};

        for (std::size_t staff = 1; staff < staffCount; ++staff)
        {
            const double previous = offsets.back();
            const double fixed =
                previous + staffHeightInSpaces + options.interStaffGap + lyricRows(staff - 1);
            const double needed = previous + reach.at(staff - 1).bottom +
                                  options.minimumStaffClearance - reach.at(staff).top;

            offsets.push_back(std::max(fixed, needed));
        }

        system.staffOffsets = offsets;

        const std::size_t last = staffCount - 1;
        const double above =
            std::max(options.spaceAboveStaff, -reach.front().top + options.textClearance);
        // Below the last staff: the room it always had, grown by however far
        // its notes pushed its text down -- and never short of the notes
        // themselves. Text in its usual place already fitted, and growing a
        // system for it would move every system with a dynamic in it.
        const double pushedDown =
            belowBase.at(last) - (staffHeightInSpaces + options.textGapBelowStaff);
        const double below = std::max(
            offsets.back() + staffHeightInSpaces + lyricRows(last) + options.spaceBelowStaff +
                pushedDown,
            offsets.back() + notes.at(last).bottom + options.textClearance);

        system.bandTop = cursor;
        system.topOfStaff = cursor + above;
        cursor = system.topOfStaff + below;
        system.bandBottom = cursor;
    }

    layout.totalHeight = cursor;
}

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

std::vector<PartKey> drawablePartsOf(const score::Score& score)
{
    std::vector<PartKey> drawable;

    for (const score::Part& part : score.parts)
    {
        bool anyPitchedStaff = false;

        for (int staff = 1; staff <= std::max(1, part.staffCount); ++staff)
        {
            anyPitchedStaff = anyPitchedStaff || isPitchedClef(openingClef(part, staff));
        }

        // The same tests `layOutScore` applies to decide a part is drawn.
        if (anyPitchedStaff && !part.measures.empty() && holdsAnyEvent(part))
        {
            drawable.push_back(PartKey{part.id, part.name});
        }
    }

    return drawable;
}

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

    // Which parts the reader hid -- unless that is every part this pass could
    // draw, in which case the selection is ignored (score-part-selection
    // design.md decision 2): an empty page is never the answer.
    const auto hiddenByChoice = [&options](const score::Part& part)
    {
        return std::find(
                   options.hiddenParts.begin(), options.hiddenParts.end(),
                   PartKey{part.id, part.name}) != options.hiddenParts.end();
    };

    const std::vector<PartKey> drawable = drawablePartsOf(*score);
    const bool anyShown = std::any_of(
        drawable.begin(), drawable.end(),
        [&options](const PartKey& key)
        {
            return std::find(options.hiddenParts.begin(), options.hiddenParts.end(), key) ==
                   options.hiddenParts.end();
        });

    std::vector<const score::Part*> hiddenParts;

    for (const score::Part& part : score->parts)
    {
        // Before any notice: a hidden part says nothing about itself.
        if (anyShown && hiddenByChoice(part))
        {
            hiddenParts.push_back(&part);

            continue;
        }

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

    // A hidden part's tempo marks go to the top staff shown, where a tempo is
    // drawn anyway, unless that staff already has one at the same moment.
    for (const score::Part* hidden : hiddenParts)
    {
        for (std::size_t measure = 0; measure < measureCount && measure < hidden->measures.size();
             ++measure)
        {
            MeasurePlan& top = plansByStaff.front().at(measure);

            for (const score::Direction& direction : hidden->measures.at(measure).directions)
            {
                if (direction.kind != score::DirectionKind::tempo)
                {
                    continue;
                }

                const auto sameMoment = [&direction](const score::Direction& other) {
                    return other.kind == score::DirectionKind::tempo &&
                           other.onset == direction.onset;
                };

                if (std::any_of(
                        top.source->directions.begin(), top.source->directions.end(), sameMoment) ||
                    std::any_of(top.borrowedTempos.begin(), top.borrowedTempos.end(), sameMoment))
                {
                    continue;
                }

                score::Direction lent = direction;
                lent.staff = top.staff;
                top.borrowedTempos.push_back(lent);
            }
        }
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

    // Each part's name, beside its staves: in full on the first system, set in
    // its indent even for a single part, and shortened on every system after
    // when there is more than one part to tell apart -- a solo line needs no
    // label once the reader is under way. Room for them comes off the width
    // the music has, so later systems -- with shorter names -- hold more of it.
    const bool nameParts = drawnParts.size() > 1;
    std::vector<std::string> fullNames;
    std::vector<std::string> shortNames;

    for (const StaffGroup& group : staffGroups)
    {
        const score::Part& part =
            *drawnParts.at(partOfStaff.at(static_cast<std::size_t>(group.firstStaff))).part;

        fullNames.push_back(displayedPartName(part.name));
        shortNames.push_back(abbreviatedPartName(part.name, part.abbreviation));
    }

    const auto nameRoomFor = [&options](const std::vector<std::string>& names, bool named)
    {
        if (!named)
        {
            return 0.0;
        }

        std::size_t longest = 0;

        for (const std::string& name : names)
        {
            // Characters, not bytes: "Klavier" and "Stimme für Gesang" are
            // measured by what is drawn.
            const auto characters = static_cast<std::size_t>(std::count_if(
                name.begin(), name.end(),
                [](char byte) { return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U; }));

            longest = std::max(longest, characters);
        }

        if (longest == 0)
        {
            return 0.0;
        }

        return std::min(
                   static_cast<double>(longest) * options.partNameCharacterWidth,
                   options.maximumPartNameWidth) +
               options.partNameGap;
    };

    const double firstNameRoom = std::max(options.firstSystemIndent, nameRoomFor(fullNames, true));
    const double laterNameRoom = nameRoomFor(shortNames, nameParts);
    const auto nameRoomOf = [firstNameRoom, laterNameRoom](std::size_t systemIndex)
    { return systemIndex == 0 ? firstNameRoom : laterNameRoom; };

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

    // Runs of empty bars, condensed into one multi-measure rest each. The run
    // is laid out as its first bar, and `barsAt` says how many bars each laid
    // out measure stands for: 1 almost everywhere.
    std::vector<int> barsAt(measureCount, 1);

    const auto emptyEverywhere = [&plansByStaff](std::size_t measure)
    {
        return std::all_of(
            plansByStaff.begin(), plansByStaff.end(),
            [measure](const std::vector<MeasurePlan>& plans)
            { return isEmptyBar(plans.at(measure)); });
    };

    for (std::size_t index = 0; index < measureCount;)
    {
        std::size_t end = index;

        while (end < measureCount && emptyEverywhere(end))
        {
            ++end;
        }

        if (end - index >= 2)
        {
            barsAt.at(index) = static_cast<int>(end - index);
            index = end;
        }
        else
        {
            index = std::max(end, index + 1);
        }
    }

    const auto widthOfBar = [&plansByStaff, &barsAt, &options](std::size_t measure)
    {
        return barsAt.at(measure) > 1 ? options.multiMeasureRestWidth
                                      : plansByStaff.front().at(measure).contentWidth;
    };

    // Pack measures into systems. At least one measure per system, however
    // wide it is: a bar that does not fit anywhere has to go somewhere, and
    // overflowing is more honest than dropping it.
    std::vector<std::vector<std::size_t>> packed;
    double cursor = 0.0;

    // What a system has for its music once its names and brace have their room.
    const auto availableFor = [&options, braceRoom, &nameRoomOf](std::size_t systemIndex)
    { return options.availableWidth - braceRoom - nameRoomOf(systemIndex); };

    // Whether a system may end after this laid-out measure at a structural
    // place (design.md decision 13): on a repeat barline either side of the
    // break, or where an ending's bracket ends.
    const auto structuralAfter = [&plansByStaff, &barsAt, measureCount](std::size_t measure)
    {
        const std::size_t last = measure + static_cast<std::size_t>(barsAt.at(measure)) - 1;
        const MeasurePlan& here = plansByStaff.front().at(last);

        if (here.repeatEnd)
        {
            return true;
        }

        if (last + 1 >= measureCount)
        {
            return false;
        }

        const MeasurePlan& next = plansByStaff.front().at(last + 1);
        const std::vector<int>& ending = here.source->repeats.endingNumbers;

        return next.repeatStart ||
               (!ending.empty() && next.source->repeats.endingNumbers != ending);
    };

    // The widths of the measures on the system being filled, parallel to
    // `packed.back()`, so one can be moved on to the next system.
    std::vector<double> widthsOnSystem;

    for (std::size_t index = 0; index < measureCount;
         index += static_cast<std::size_t>(barsAt.at(index)))
    {
        // The same on every staff: spaceMeasure gives a measure one width.
        const double contentWidth = widthOfBar(index);

        const bool wouldStartSystem = packed.empty() || packed.back().empty();

        const double width = headerWidthAt(index, wouldStartSystem) + contentWidth;

        if (!wouldStartSystem && cursor + width > availableFor(packed.size() - 1))
        {
            // The fill would break before this measure. If that is an ordinary
            // place and the bar before is a structural one, break there
            // instead -- as long as what is left is three quarters full.
            std::vector<std::size_t> carried;
            std::vector<std::size_t>& filling = packed.back();

            if (filling.size() >= 2 && !structuralAfter(filling.back()) &&
                structuralAfter(filling.at(filling.size() - 2)) &&
                cursor - widthsOnSystem.back() >=
                    structuralBreakMinimumFill * availableFor(packed.size() - 1))
            {
                carried.push_back(filling.back());
                filling.pop_back();
            }

            packed.emplace_back();
            widthsOnSystem.clear();
            cursor = 0.0;

            for (const std::size_t moved : carried)
            {
                const double movedWidth =
                    headerWidthAt(moved, packed.back().empty()) + widthOfBar(moved);
                packed.back().push_back(moved);
                widthsOnSystem.push_back(movedWidth);
                cursor += movedWidth;
            }

            // A measure carried over and this one may not fit together; then
            // this one starts a system of its own, as the greedy fill would.
            if (!packed.back().empty() && cursor + headerWidthAt(index, false) + contentWidth >
                                              availableFor(packed.size() - 1))
            {
                packed.emplace_back();
                widthsOnSystem.clear();
                cursor = 0.0;
            }

            const double placed = headerWidthAt(index, packed.back().empty()) + contentWidth;
            packed.back().push_back(index);
            widthsOnSystem.push_back(placed);
            cursor += placed;

            continue;
        }

        if (packed.empty())
        {
            packed.emplace_back();
        }

        cursor += width;
        packed.back().push_back(index);
        widthsOnSystem.push_back(width);
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

    // One map per part: a NoteRef addresses a note within its own part, so two
    // parts' first notes have the same ref.
    std::vector<NotePlacementMap> placements(drawnParts.size());

    layout.systems.reserve(packed.size());

    for (std::size_t systemIndex = 0; systemIndex < packed.size(); ++systemIndex)
    {
        const std::vector<std::size_t>& measures = packed.at(systemIndex);

        // Where it sits vertically, and how far apart its staves are, is
        // decided once its text is placed: see placeTextAndStaves.
        System system;
        system.firstMeasureIndex = plansByStaff.front().at(measures.front()).index;
        system.lastMeasureIndex =
            plansByStaff.front()
                .at(measures.back() + static_cast<std::size_t>(barsAt.at(measures.back())) - 1)
                .index;
        system.staffOffsets = staffOffsets;
        system.staffGroups = staffGroups;
        system.staffLyricVerses = staffVerses;
        const double nameRoom = nameRoomOf(systemIndex);
        system.staffStartX = nameRoom + braceRoom;

        if (nameRoom > 0.0)
        {
            const std::vector<std::string>& names = systemIndex == 0 ? fullNames : shortNames;

            for (std::size_t group = 0; group < staffGroups.size(); ++group)
            {
                if (!names.at(group).empty())
                {
                    system.partNames.push_back(PartNameGlyph{
                        names.at(group), nameRoom - options.partNameGap,
                        staffGroups.at(group).firstStaff, staffGroups.at(group).lastStaff});
                }
            }
        }

        // Justification (design.md decision 10): every system but the last is
        // stretched to the width it has, by scaling the room inside its
        // measures. Headers keep their width -- a clef stretched is a clef
        // misdrawn -- and a system is never squeezed, so one that overflows
        // with a single wide bar stays as it is.
        double stretch = 1.0;

        if (systemIndex + 1 < packed.size())
        {
            double natural = 0.0;
            double content = 0.0;

            for (std::size_t slot = 0; slot < measures.size(); ++slot)
            {
                const double width = widthOfBar(measures.at(slot));
                natural += headerWidthAt(measures.at(slot), slot == 0) + width;
                content += width;
            }

            const double spare = availableFor(systemIndex) - natural;

            if (spare > 0.0 && content > 0.0)
            {
                stretch = (content + spare) / content;
            }
        }

        double x = system.staffStartX;
        std::vector<MeasureSpan> spans;

        for (std::size_t slot = 0; slot < measures.size(); ++slot)
        {
            const std::size_t measure = measures.at(slot);
            const bool startsSystem = slot == 0;
            const int bars = barsAt.at(measure);
            const bool isLastMeasure = measure + static_cast<std::size_t>(bars) == measureCount;

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

            if (startsSystem)
            {
                // A system always opens with at least a clef, so the content
                // start is the header's end plus the gap after a header and the
                // lead-in; take both back off.
                system.headerEndX = contentStart - contentLeadIn - headerGap;
            }

            double barlineX = x;

            for (std::size_t staff = 0; staff < plansByStaff.size(); ++staff)
            {
                const GlyphCounts before = glyphCountsOf(system);

                barlineX = emitMeasure(
                    system, systemIndex, plansByStaff.at(staff).at(measure), startsSystem,
                    isLastMeasure, options, placements.at(partOfStaff.at(staff)), x, contentStart,
                    static_cast<int>(staff), stretch, bars);

                assignStaff(system, before, static_cast<int>(staff));
            }

            spans.push_back(MeasureSpan{plansByStaff.front().at(measure).source, x, barlineX});
            x = barlineX;
        }

        system.width = x;

        addJumps(system, spans);

        addVoltas(
            system, spans,
            measures.front() == 0 ? nullptr : plansByStaff.front().at(measures.front() - 1).source);

        liftTempoMarksToTop(system, staffGroups.front().lastStaff);

        layout.systems.push_back(std::move(system));
    }

    // Ties, once every notehead has a system and an x. Each is drawn on the
    // staff of the note it leaves, bowing away from that note's stem. A tie
    // whose ends landed on different systems is drawn as two halves (design.md
    // decision 5): from its note to the end of the system, then from just
    // after the next system's clef and key to the note it ties into. Both halves bow the same way,
    // so they read as one tie.
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

                    const NotePlacement& from = start->second;
                    const NotePlacement& to = end->second;
                    const bool above = !from.stemUp;

                    if (from.systemIndex == to.systemIndex)
                    {
                        TieGlyph tie{from.x, to.x, from.position, above};
                        tie.staff = from.staff;
                        layout.systems.at(from.systemIndex).ties.push_back(tie);

                        continue;
                    }

                    System& leaving = layout.systems.at(from.systemIndex);
                    System& arriving = layout.systems.at(to.systemIndex);

                    TieGlyph first{from.x, leaving.width - tieHalfEndInset, from.position, above};
                    first.staff = from.staff;
                    leaving.ties.push_back(first);

                    // On the arriving system's copy of the staff: the same staff
                    // of the same part, which is the note's own.
                    TieGlyph second{
                        arriving.headerEndX + tieHalfStartGap, to.x, to.position, above};
                    second.staff = to.staff;
                    second.fromSystemStart = true;
                    arriving.ties.push_back(second);
                }
            }
        }
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
    placeTextAndStaves(layout, options);
    layout.notices = notices.take();

    return layout;
}
} // namespace engraving
