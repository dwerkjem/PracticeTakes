#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/ScoreLayoutEngine.h"

#include "support/ScoreFixtures.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace engraving;
using score::Tick;
using score::ticksPerQuarterNote;
using testing::score::notesOf;

namespace
{
[[nodiscard]] score::ScoreEvent noteAt(Tick onset, Tick duration, score::Pitch pitch)
{
    score::ScoreEvent event;
    event.kind = score::EventKind::note;
    event.onset = onset;
    event.duration = duration;
    event.notes = notesOf({pitch});

    return event;
}

[[nodiscard]] score::ScoreEvent restAt(Tick onset, Tick duration)
{
    score::ScoreEvent event;
    event.kind = score::EventKind::rest;
    event.onset = onset;
    event.duration = duration;

    return event;
}

[[nodiscard]] score::Pitch pitch(score::Step step, int alter, int octave)
{
    return score::pitchFromSpelling(step, alter, octave);
}

[[nodiscard]] score::Voice voiceOf(int number, std::vector<score::ScoreEvent> events)
{
    score::Voice voice;
    voice.number = number;
    voice.events = std::move(events);

    return voice;
}

[[nodiscard]] score::Measure
measureOf(std::size_t index, std::vector<score::Voice> voices, bool declareAttributes = false)
{
    score::Measure measure;
    measure.index = index;
    measure.printedNumber = std::to_string(index + 1);
    measure.nominalDuration = score::nominalMeasureTicks(4, 4);
    measure.start = static_cast<Tick>(index) * measure.nominalDuration;
    measure.voices = std::move(voices);

    if (declareAttributes)
    {
        measure.attributes.clefs = {score::Clef{"G", 2, 0, 1}};
        measure.attributes.key = score::KeySignature{0, "major"};
        measure.attributes.time = score::TimeSignature{4, 4};
    }

    return measure;
}

// One treble part, declaring its attributes in the first measure the way a
// MusicXML file does.
[[nodiscard]] std::shared_ptr<const score::Score>
scoreOf(std::vector<score::Measure> measures, int staffCount = 1)
{
    score::Part part;
    part.id = "P1";
    part.name = "Soprano";
    part.staffCount = staffCount;
    part.measures = std::move(measures);

    auto built = std::make_shared<score::Score>();
    built->metadata.workTitle = "A Layout Test";
    built->parts = {part};

    return built;
}

// A roomy width, so a case about one bar is not also a case about system
// breaking.
[[nodiscard]] LayoutOptions wideOptions()
{
    LayoutOptions options;
    options.availableWidth = 400.0;

    return options;
}

[[nodiscard]] bool anyNoticeContains(const ScoreLayout& layout, const std::string& fragment)
{
    return std::any_of(
        layout.notices.begin(), layout.notices.end(), [&fragment](const std::string& notice)
        { return notice.find(fragment) != std::string::npos; });
}
} // namespace

TEST_CASE("no score open is an outcome, not an error", "[score][engraving][layout]")
{
    const ScoreLayout layout = layOutScore(nullptr, wideOptions());

    CHECK(layout.outcome == LayoutOutcome::noScore);
    CHECK(layout.systems.empty());
    CHECK(layout.notices.empty());
    CHECK(layout.totalHeight == 0.0);
}

TEST_CASE("a score with nothing in it says so", "[score][engraving][layout]")
{
    SECTION("no parts")
    {
        auto empty = std::make_shared<score::Score>();

        const ScoreLayout layout = layOutScore(empty, wideOptions());

        CHECK(layout.outcome == LayoutOutcome::nothingToDraw);
        CHECK(anyNoticeContains(layout, "no parts"));
    }

    SECTION("a part with no events")
    {
        const ScoreLayout layout =
            layOutScore(scoreOf({measureOf(0, {voiceOf(1, {})}, true)}), wideOptions());

        CHECK(layout.outcome == LayoutOutcome::nothingToDraw);
        CHECK(anyNoticeContains(layout, "no notes or rests"));
    }
}

TEST_CASE("one bar of one part lays out on one system", "[score][engraving][layout]")
{
    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(
            0,
            {voiceOf(
                1, {noteAt(0, ticksPerQuarterNote, pitch(score::Step::g, 0, 4)),
                    noteAt(ticksPerQuarterNote, ticksPerQuarterNote, pitch(score::Step::a, 0, 4)),
                    noteAt(
                        ticksPerQuarterNote * 2, ticksPerQuarterNote * 2,
                        pitch(score::Step::b, 0, 4))})},
            true)}),
        wideOptions());

    REQUIRE(layout.outcome == LayoutOutcome::laidOut);
    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    CHECK(layout.scoreTitle == "A Layout Test");
    CHECK(layout.partName == "Soprano");
    CHECK(system.noteheads.size() == 3);

    // Clef, key and time all declared in the first measure. A key of no
    // accidentals draws none of them, which is why only two of the three
    // produce a glyph.
    CHECK(system.clefs.size() == 1);
    CHECK(system.keyAccidentals.empty());
    CHECK(system.timeSignatures.size() == 1);

    // One barline, and it is the end of the score.
    REQUIRE(system.barlines.size() == 1);
    CHECK(system.barlines.front().kind == BarlineKind::final);

    CHECK(system.firstMeasureIndex == 0);
    CHECK(system.lastMeasureIndex == 0);
}

TEST_CASE("notes are placed in onset order, left to right", "[score][engraving][layout]")
{
    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(
            0,
            {voiceOf(
                1,
                {noteAt(0, ticksPerQuarterNote, pitch(score::Step::c, 0, 5)),
                 noteAt(ticksPerQuarterNote, ticksPerQuarterNote, pitch(score::Step::d, 0, 5)),
                 noteAt(ticksPerQuarterNote * 2, ticksPerQuarterNote, pitch(score::Step::e, 0, 5)),
                 noteAt(
                     ticksPerQuarterNote * 3, ticksPerQuarterNote, pitch(score::Step::f, 0, 5))})},
            true)}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<NoteheadGlyph>& heads = layout.systems.front().noteheads;

    REQUIRE(heads.size() == 4);

    for (std::size_t index = 1; index < heads.size(); ++index)
    {
        INFO("notehead " << index);
        CHECK(heads.at(index).x > heads.at(index - 1).x);
    }

    // Descending staff positions, because the pitches ascend.
    for (std::size_t index = 1; index < heads.size(); ++index)
    {
        CHECK(heads.at(index).position < heads.at(index - 1).position);
    }

    // Four equal quarter notes are equally spaced, which is what proportional
    // spacing means and what a reader expects of a bar of crotchets.
    const double firstGap = heads.at(1).x - heads.at(0).x;

    CHECK(heads.at(2).x - heads.at(1).x == Catch::Approx(firstGap));
    CHECK(heads.at(3).x - heads.at(2).x == Catch::Approx(firstGap));
}

TEST_CASE("a chord is one stem and several noteheads", "[score][engraving][layout]")
{
    score::ScoreEvent chord;
    chord.kind = score::EventKind::chord;
    chord.onset = 0;
    chord.duration = ticksPerQuarterNote;
    chord.notes = notesOf(
        {pitch(score::Step::c, 0, 4), pitch(score::Step::e, 0, 4), pitch(score::Step::g, 0, 4)});

    const ScoreLayout layout =
        layOutScore(scoreOf({measureOf(0, {voiceOf(1, {chord})}, true)}), wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    CHECK(system.noteheads.size() == 3);
    CHECK(system.stems.size() == 1);

    // The stem attaches at the lowest note of an upward-stemmed chord and
    // reaches past the highest.
    const StemGlyph& stem = system.stems.front();

    CHECK(stem.up);
    CHECK(stem.attachPosition == 10);
    CHECK(stem.tipPosition < 6);
}

TEST_CASE("two noteheads a second apart do not share an x", "[score][engraving][layout]")
{
    score::ScoreEvent chord;
    chord.kind = score::EventKind::chord;
    chord.onset = 0;
    chord.duration = ticksPerQuarterNote;
    chord.notes = notesOf({pitch(score::Step::c, 0, 5), pitch(score::Step::d, 0, 5)});

    const ScoreLayout layout =
        layOutScore(scoreOf({measureOf(0, {voiceOf(1, {chord})}, true)}), wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<NoteheadGlyph>& heads = layout.systems.front().noteheads;

    REQUIRE(heads.size() == 2);
    CHECK(heads.at(0).x != heads.at(1).x);
}

TEST_CASE("a rest is drawn as a rest, not as a silent note", "[score][engraving][layout]")
{
    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(
            0,
            {voiceOf(
                1, {noteAt(0, ticksPerQuarterNote * 2, pitch(score::Step::g, 0, 4)),
                    restAt(ticksPerQuarterNote * 2, ticksPerQuarterNote * 2)})},
            true)}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    CHECK(system.noteheads.size() == 1);
    REQUIRE(system.rests.size() == 1);
    CHECK(system.rests.front().value == WrittenValue::half);
    CHECK(system.rests.front().x > system.noteheads.front().x);
}

TEST_CASE("a bar holding only a rest centres it", "[score][engraving][layout]")
{
    const ScoreLayout layout = layOutScore(
        scoreOf(
            {measureOf(
                 0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4))})},
                 true),
             measureOf(1, {voiceOf(1, {restAt(0, ticksPerQuarterNote * 4)})})}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    REQUIRE(system.rests.size() == 1);
    REQUIRE(system.barlines.size() == 2);

    // Between the two barlines rather than hard against the first of them.
    const double openingBarline = system.barlines.at(0).x;

    CHECK(system.rests.front().x > openingBarline);
    CHECK(system.rests.front().x < system.barlines.at(1).x);
}

TEST_CASE("an accidental is attached to the notehead that needs it", "[score][engraving][layout]")
{
    score::Measure measure = measureOf(
        0,
        {voiceOf(
            1,
            {noteAt(0, ticksPerQuarterNote, pitch(score::Step::f, 1, 4)),
             noteAt(ticksPerQuarterNote, ticksPerQuarterNote, pitch(score::Step::f, 1, 4)),
             noteAt(
                 ticksPerQuarterNote * 2, ticksPerQuarterNote * 2, pitch(score::Step::f, 0, 4))})},
        true);

    const ScoreLayout layout = layOutScore(scoreOf({measure}), wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<NoteheadGlyph>& heads = layout.systems.front().noteheads;

    REQUIRE(heads.size() == 3);

    // First F-sharp carries the sign, the second does not, and the natural
    // cancels it -- the whole of the in-bar rule, in one bar.
    CHECK(heads.at(0).accidental == AccidentalKind::sharp);
    CHECK(heads.at(1).accidental == AccidentalKind::none);
    CHECK(heads.at(2).accidental == AccidentalKind::natural);

    // All three on the same line, because an accidental never moves a note.
    CHECK(heads.at(0).position == heads.at(1).position);
    CHECK(heads.at(1).position == heads.at(2).position);
}

TEST_CASE("a key signature is drawn, and repeats at every system", "[score][engraving][layout]")
{
    std::vector<score::Measure> measures;

    for (std::size_t index = 0; index < 12; ++index)
    {
        score::Measure measure = measureOf(
            index, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::d, 0, 5))})});

        if (index == 0)
        {
            measure.attributes.clefs = {score::Clef{"G", 2, 0, 1}};
            measure.attributes.key = score::KeySignature{3, "major"};
            measure.attributes.time = score::TimeSignature{4, 4};
        }

        measures.push_back(measure);
    }

    LayoutOptions options;
    options.availableWidth = 70.0;

    const ScoreLayout layout = layOutScore(scoreOf(measures), options);

    REQUIRE(layout.outcome == LayoutOutcome::laidOut);
    REQUIRE(layout.systems.size() > 1);

    for (const System& system : layout.systems)
    {
        INFO("system starting at measure " << system.firstMeasureIndex);

        // Clef and key repeat at every system start, which is the convention.
        CHECK(system.clefs.size() >= 1);
        CHECK(system.keyAccidentals.size() >= 3);
    }

    // The time signature does not repeat: it is drawn where it changes, and it
    // changes once.
    std::size_t timeSignatures = 0;

    for (const System& system : layout.systems)
    {
        timeSignatures += system.timeSignatures.size();
    }

    CHECK(timeSignatures == 1);
}

TEST_CASE("a clef change is drawn and moves the notes after it", "[score][engraving][layout]")
{
    score::Measure second = measureOf(
        1, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::c, 0, 4))})});
    second.attributes.clefs = {score::Clef{"F", 4, 0, 1}};

    const ScoreLayout layout = layOutScore(
        scoreOf(
            {measureOf(
                 0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::c, 0, 4))})},
                 true),
             second}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    // Two clefs on one system: the one the part opens in and the change.
    CHECK(system.clefs.size() == 2);

    REQUIRE(system.noteheads.size() == 2);

    // The same middle C, one ledger line below a treble staff and one above a
    // bass staff.
    CHECK(system.noteheads.at(0).position == bottomLineSteps + 2);
    CHECK(system.noteheads.at(1).position == topLineSteps - 2);
    CHECK(system.noteheads.at(0).ledger.below == 1);
    CHECK(system.noteheads.at(1).ledger.above == 1);
}

TEST_CASE("measures break into systems at the available width", "[score][engraving][layout]")
{
    std::vector<score::Measure> measures;

    for (std::size_t index = 0; index < 16; ++index)
    {
        measures.push_back(measureOf(
            index, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::b, 0, 4))})},
            index == 0));
    }

    LayoutOptions narrow;
    narrow.availableWidth = 80.0;

    const ScoreLayout layout = layOutScore(scoreOf(measures), narrow);

    REQUIRE(layout.outcome == LayoutOutcome::laidOut);
    REQUIRE(layout.systems.size() > 1);

    // Every measure appears exactly once, in order, across the systems.
    std::vector<std::size_t> firstIndices;

    for (const System& system : layout.systems)
    {
        firstIndices.push_back(system.firstMeasureIndex);

        CHECK(system.lastMeasureIndex >= system.firstMeasureIndex);
    }

    CHECK(std::is_sorted(firstIndices.begin(), firstIndices.end()));
    CHECK(layout.systems.front().firstMeasureIndex == 0);
    CHECK(layout.systems.back().lastMeasureIndex == 15);

    // Stacked downwards, and the total is tall enough to hold them all.
    for (std::size_t index = 1; index < layout.systems.size(); ++index)
    {
        CHECK(layout.systems.at(index).topOfStaff > layout.systems.at(index - 1).topOfStaff);
    }

    CHECK(layout.totalHeight > layout.systems.back().topOfStaff);

    // A wider tool fits more of them, which is the whole point of laying out
    // for a width.
    LayoutOptions wide;
    wide.availableWidth = 320.0;

    CHECK(layOutScore(scoreOf(measures), wide).systems.size() < layout.systems.size());
}

// The property that makes the layout snapshot testable at all, and the reason
// it is a value rather than something computed while painting.
TEST_CASE("the same score at the same width lays out identically", "[score][engraving][layout]")
{
    std::vector<score::Measure> measures;

    for (std::size_t index = 0; index < 9; ++index)
    {
        measures.push_back(measureOf(
            index,
            {voiceOf(
                1,
                {noteAt(0, ticksPerQuarterNote, pitch(score::Step::g, 1, 4)),
                 noteAt(
                     ticksPerQuarterNote, ticksPerQuarterNote * 3, pitch(score::Step::a, -1, 4))})},
            index == 0));
    }

    const ScoreLayout first = layOutScore(scoreOf(measures), wideOptions());
    const ScoreLayout second = layOutScore(scoreOf(measures), wideOptions());

    REQUIRE(first.systems.size() == second.systems.size());
    CHECK(first.totalHeight == second.totalHeight);
    CHECK(first.notices == second.notices);

    for (std::size_t index = 0; index < first.systems.size(); ++index)
    {
        const System& left = first.systems.at(index);
        const System& right = second.systems.at(index);

        REQUIRE(left.noteheads.size() == right.noteheads.size());
        CHECK(left.width == right.width);
        CHECK(left.topOfStaff == right.topOfStaff);

        for (std::size_t head = 0; head < left.noteheads.size(); ++head)
        {
            CHECK(left.noteheads.at(head).x == right.noteheads.at(head).x);
            CHECK(left.noteheads.at(head).position == right.noteheads.at(head).position);
            CHECK(left.noteheads.at(head).accidental == right.noteheads.at(head).accidental);
        }
    }
}

TEST_CASE("a tie becomes a curve between its two noteheads", "[score][engraving][layout]")
{
    const score::NoteRef firstNote{0, 0, 0, 0};
    const score::NoteRef secondNote{1, 0, 0, 0};

    score::ScoreEvent start = noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4));
    start.notes.front().tiedTo = secondNote;

    score::ScoreEvent stop = noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4));
    stop.notes.front().tiedFrom = firstNote;

    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(0, {voiceOf(1, {start})}, true), measureOf(1, {voiceOf(1, {stop})})}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    REQUIRE(system.ties.size() == 1);
    REQUIRE(system.noteheads.size() == 2);

    const TieGlyph& tie = system.ties.front();

    CHECK(tie.startX == system.noteheads.at(0).x);
    CHECK(tie.endX == system.noteheads.at(1).x);
    CHECK(tie.position == system.noteheads.at(0).position);

    // The note being held on carries no accidental of its own, which is the
    // rule MeasureAccidentals owns and this is the end-to-end proof of it.
    CHECK(system.noteheads.at(1).accidental == AccidentalKind::none);
}

TEST_CASE("a tie split across two systems is reported, not drawn", "[score][engraving][layout]")
{
    std::vector<score::Measure> measures;

    for (std::size_t index = 0; index < 8; ++index)
    {
        score::ScoreEvent event = noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::b, 0, 4));

        // Tie every bar to the next, so wherever the break falls one tie
        // crosses it.
        if (index + 1 < 8)
        {
            event.notes.front().tiedTo = score::NoteRef{index + 1, 0, 0, 0};
        }

        if (index > 0)
        {
            event.notes.front().tiedFrom = score::NoteRef{index - 1, 0, 0, 0};
        }

        measures.push_back(measureOf(index, {voiceOf(1, {event})}, index == 0));
    }

    LayoutOptions narrow;
    narrow.availableWidth = 75.0;

    const ScoreLayout layout = layOutScore(scoreOf(measures), narrow);

    REQUIRE(layout.systems.size() > 1);
    CHECK(anyNoticeContains(layout, "different lines of the layout"));
}

TEST_CASE("lyrics are carried through to the layout", "[score][engraving][layout]")
{
    score::ScoreEvent first = noteAt(0, ticksPerQuarterNote * 2, pitch(score::Step::g, 0, 4));
    first.lyrics = {
        score::LyricSyllable{1, score::SyllabicPosition::begin, "A", false},
        score::LyricSyllable{2, score::SyllabicPosition::single, "Two", false}};

    score::ScoreEvent second =
        noteAt(ticksPerQuarterNote * 2, ticksPerQuarterNote * 2, pitch(score::Step::a, 0, 4));
    second.lyrics = {score::LyricSyllable{1, score::SyllabicPosition::end, "men", false}};

    const ScoreLayout layout =
        layOutScore(scoreOf({measureOf(0, {voiceOf(1, {first, second})}, true)}), wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    REQUIRE(system.lyrics.size() == 3);

    // Two verses, which is what a caller sizing a system needs to know without
    // walking the glyphs.
    CHECK(layout.lyricVerseCount == 2);

    // A syllable that begins a word is drawn with a hyphen after it; one that
    // ends a word is not. The model keeps that distinction precisely because it
    // cannot be recovered from the text.
    CHECK(system.lyrics.at(0).hyphenAfter);
    CHECK_FALSE(system.lyrics.at(2).hyphenAfter);

    // Each syllable sits under its own note.
    CHECK(system.lyrics.at(0).x == system.noteheads.at(0).x);
    CHECK(system.lyrics.at(2).x == system.noteheads.at(1).x);
}

TEST_CASE("two voices on a staff stem by voice, not by pitch", "[score][engraving][layout]")
{
    // Both notes are below the middle line, so the single-voice rule would stem
    // both of them up and make the two voices unreadable. Half notes rather
    // than whole ones because a whole note is drawn without a stem at all.
    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(
            0,
            {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 2, pitch(score::Step::g, 0, 4))}),
             voiceOf(2, {noteAt(0, ticksPerQuarterNote * 2, pitch(score::Step::e, 0, 4))})},
            true)}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<StemGlyph>& stems = layout.systems.front().stems;

    REQUIRE(stems.size() == 2);
    CHECK(stems.at(0).up);
    CHECK_FALSE(stems.at(1).up);
}

TEST_CASE("one voice stems by pitch", "[score][engraving][layout]")
{
    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(
            0,
            {voiceOf(
                1, {noteAt(0, ticksPerQuarterNote * 2, pitch(score::Step::g, 0, 4)),
                    noteAt(
                        ticksPerQuarterNote * 2, ticksPerQuarterNote * 2,
                        pitch(score::Step::g, 0, 5))})},
            true)}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<StemGlyph>& stems = layout.systems.front().stems;

    REQUIRE(stems.size() == 2);

    // Below the middle line, up; above it, down.
    CHECK(stems.at(0).up);
    CHECK_FALSE(stems.at(1).up);
}

TEST_CASE("a grace note is drawn small and takes no time", "[score][engraving][layout]")
{
    score::ScoreEvent grace;
    grace.kind = score::EventKind::note;
    grace.onset = ticksPerQuarterNote;
    grace.duration = 0;
    grace.isGrace = true;
    grace.notes = notesOf({pitch(score::Step::f, 0, 5)});

    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(
            0,
            {voiceOf(
                1,
                {noteAt(0, ticksPerQuarterNote, pitch(score::Step::c, 0, 5)), grace,
                 noteAt(
                     ticksPerQuarterNote, ticksPerQuarterNote * 3, pitch(score::Step::g, 0, 4))})},
            true)}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const System& system = layout.systems.front();

    REQUIRE(system.noteheads.size() == 3);

    const auto graceHead = std::find_if(
        system.noteheads.begin(), system.noteheads.end(),
        [](const NoteheadGlyph& glyph) { return glyph.isGrace; });

    REQUIRE(graceHead != system.noteheads.end());

    // Immediately before the beat it leads into, taking no horizontal time of
    // its own: the note it precedes is still where its onset puts it.
    const auto main = std::find_if(
        system.noteheads.begin(), system.noteheads.end(),
        [](const NoteheadGlyph& glyph) { return !glyph.isGrace && glyph.source.eventIndex == 2; });

    REQUIRE(main != system.noteheads.end());
    CHECK(graceHead->x < main->x);
}

TEST_CASE("repeat barlines are drawn from the measure's markings", "[score][engraving][layout]")
{
    score::Measure first = measureOf(
        0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4))})}, true);
    first.repeats.repeatStart = true;

    score::Measure second = measureOf(
        1, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::a, 0, 4))})});
    second.repeats.repeatEnd = true;
    second.repeats.endingNumbers = {1};
    second.repeats.jumps = {"Fine"};

    const ScoreLayout layout = layOutScore(scoreOf({first, second}), wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<BarlineGlyph>& barlines = layout.systems.front().barlines;

    CHECK(std::any_of(
        barlines.begin(), barlines.end(),
        [](const BarlineGlyph& glyph) { return glyph.kind == BarlineKind::repeatStart; }));
    CHECK(std::any_of(
        barlines.begin(), barlines.end(),
        [](const BarlineGlyph& glyph) { return glyph.kind == BarlineKind::repeatEnd; }));

    // The markings this pass carries but does not draw say so, rather than
    // being dropped on the floor.
    CHECK(anyNoticeContains(layout, "volta"));
    CHECK(anyNoticeContains(layout, "Jump markings"));
}

TEST_CASE("every notehead names the note it came from", "[score][engraving][layout]")
{
    // The identity #33's target-note overlay and #41's live display will need
    // to find a glyph without laying the score out again.
    const ScoreLayout layout = layOutScore(
        scoreOf(
            {measureOf(
                 0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4))})},
                 true),
             measureOf(
                 1,
                 {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::a, 0, 4))})})}),
        wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<NoteheadGlyph>& heads = layout.systems.front().noteheads;

    REQUIRE(heads.size() == 2);
    CHECK(heads.at(0).source == score::NoteRef{0, 0, 0, 0});
    CHECK(heads.at(1).source == score::NoteRef{1, 0, 0, 0});
}

TEST_CASE("what is not drawn is named", "[score][engraving][layout]")
{
    SECTION("beams, always, because this pass has none")
    {
        const ScoreLayout layout = layOutScore(
            scoreOf({measureOf(
                0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4))})},
                true)}),
            wideOptions());

        CHECK(anyNoticeContains(layout, "flags rather than beamed"));
    }

    SECTION("further parts, by name")
    {
        score::Part second;
        second.id = "P2";
        second.name = "Piano";
        second.measures = {measureOf(
            0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::c, 0, 3))})},
            true)};

        auto built = std::make_shared<score::Score>();
        built->metadata.workTitle = "A Duet";

        score::Part first;
        first.id = "P1";
        first.name = "Voice";
        first.measures = {measureOf(
            0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4))})},
            true)};

        built->parts = {first, second};

        const ScoreLayout layout = layOutScore(built, wideOptions());

        CHECK(layout.partName == "Voice");
        CHECK(anyNoticeContains(layout, "Piano"));
        CHECK(anyNoticeContains(layout, "#33"));
    }

    SECTION("further staves of the drawn part, and what was on them")
    {
        score::ScoreEvent onSecondStaff =
            noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::c, 0, 3));
        onSecondStaff.staff = 2;

        const ScoreLayout layout = layOutScore(
            scoreOf(
                {measureOf(
                    0,
                    {voiceOf(
                        1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4)),
                            onSecondStaff})},
                    true)},
                2),
            wideOptions());

        REQUIRE(layout.systems.size() == 1);

        // Only the first staff's note is drawn, and the other one is accounted
        // for rather than silently absent.
        CHECK(layout.systems.front().noteheads.size() == 1);
        CHECK(anyNoticeContains(layout, "#200"));
        CHECK(anyNoticeContains(layout, "another staff"));
    }

    SECTION("a duration no written value matches")
    {
        const ScoreLayout layout = layOutScore(
            scoreOf(
                {measureOf(0, {voiceOf(1, {noteAt(0, 1000, pitch(score::Step::g, 0, 4))})}, true)}),
            wideOptions());

        CHECK(anyNoticeContains(layout, "no written note value"));
    }
}

TEST_CASE(
    "a part written in an unpitched clef is refused, not misplaced",
    "[score][engraving][layout]")
{
    score::Measure measure = measureOf(
        0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::c, 0, 4))})});
    measure.attributes.clefs = {score::Clef{"percussion", 3, 0, 1}};

    const ScoreLayout layout = layOutScore(scoreOf({measure}), wideOptions());

    CHECK(layout.outcome == LayoutOutcome::nothingToDraw);
    CHECK(layout.systems.empty());
    CHECK(anyNoticeContains(layout, "names no pitch"));
}

TEST_CASE("tempo and dynamic markings reach the layout", "[score][engraving][layout]")
{
    score::Measure measure = measureOf(
        0, {voiceOf(1, {noteAt(0, ticksPerQuarterNote * 4, pitch(score::Step::g, 0, 4))})}, true);

    measure.directions = {
        score::Direction{score::DirectionKind::tempo, 0, 1, "Andante", 72.0},
        score::Direction{score::DirectionKind::dynamic, 0, 1, "mf", std::nullopt}};

    const ScoreLayout layout = layOutScore(scoreOf({measure}), wideOptions());

    REQUIRE(layout.systems.size() == 1);

    const std::vector<TextGlyph>& texts = layout.systems.front().texts;

    CHECK(std::any_of(
        texts.begin(), texts.end(),
        [](const TextGlyph& glyph)
        {
            return glyph.role == TextRole::tempo &&
                   glyph.text.find("Andante") != std::string::npos &&
                   glyph.text.find("72") != std::string::npos;
        }));

    CHECK(std::any_of(
        texts.begin(), texts.end(), [](const TextGlyph& glyph)
        { return glyph.role == TextRole::dynamic && glyph.text == "mf"; }));

    // The printed bar number, at the start of the system.
    CHECK(std::any_of(
        texts.begin(), texts.end(), [](const TextGlyph& glyph)
        { return glyph.role == TextRole::measureNumber && glyph.text == "1"; }));
}

TEST_CASE("a bar that cannot fit anywhere is still laid out", "[score][engraving][layout]")
{
    // A measure wider than the tool has to go somewhere. Overflowing the width
    // is more honest than dropping it, and the system's own width says it
    // happened.
    LayoutOptions tiny;
    tiny.availableWidth = 6.0;

    const ScoreLayout layout = layOutScore(
        scoreOf({measureOf(
            0,
            {voiceOf(
                1, {noteAt(0, ticksPerQuarterNote, pitch(score::Step::c, 0, 5)),
                    noteAt(ticksPerQuarterNote, ticksPerQuarterNote, pitch(score::Step::d, 0, 5)),
                    noteAt(
                        ticksPerQuarterNote * 2, ticksPerQuarterNote * 2,
                        pitch(score::Step::e, 0, 5))})},
            true)}),
        tiny);

    REQUIRE(layout.outcome == LayoutOutcome::laidOut);
    REQUIRE(layout.systems.size() == 1);
    CHECK(layout.systems.front().noteheads.size() == 3);
    CHECK(layout.systems.front().width > tiny.availableWidth);
}
