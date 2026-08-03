#include <catch2/catch_test_macros.hpp>

#include "support/ScoreFixtures.h"

#include "platform/score/Score.h"

using namespace score;
using testing::score::notesOf;

namespace
{
// A minimal two-bar, one-part score built by hand. The model is constructible
// without a parser on purpose -- that is what lets its rules be tested before
// any XML is read.
[[nodiscard]] Score makeTwoBarScore()
{
    ScoreEvent note;
    note.kind = EventKind::note;
    note.onset = 0;
    note.duration = ticksPerQuarterNote;
    note.notes = notesOf({pitchFromSpelling(Step::c, 0, 4)});

    Voice voice;
    voice.number = 1;
    voice.events = {note};

    Measure first;
    first.printedNumber = "1";
    first.index = 0;
    first.start = 0;
    first.nominalDuration = nominalMeasureTicks(4, 4);
    first.voices = {voice};

    Measure second = first;
    second.printedNumber = "2";
    second.index = 1;
    second.start = first.nominalDuration;

    Part part;
    part.id = "P1";
    part.name = "Soprano";
    part.measures = {first, second};

    Score score;
    score.parts = {part};

    return score;
}
} // namespace

TEST_CASE("a score reports its measure count and total length", "[score][model]")
{
    const Score score = makeTwoBarScore();

    CHECK(measureCount(score) == 2);
    CHECK(totalLength(score) == nominalMeasureTicks(4, 4) * 2);
}

TEST_CASE("an empty score has no measures and no length", "[score][model]")
{
    const Score empty;

    CHECK(measureCount(empty) == 0);
    CHECK(totalLength(empty) == 0);

    Score partWithNoMeasures;
    partWithNoMeasures.parts = {Part{}};

    CHECK(measureCount(partWithNoMeasures) == 0);
    CHECK(totalLength(partWithNoMeasures) == 0);
}

TEST_CASE("a score states the tick base it is denominated in", "[score][model]")
{
    // Consumers should never have to assume the tick base; a future change of
    // base must be detectable rather than silent.
    const Score score;

    CHECK(score.ticksPerQuarterNote == score::ticksPerQuarterNote);
}

TEST_CASE("a default score has a usable tempo map", "[score][model]")
{
    const Score score;

    REQUIRE(score.tempoMap.entries().size() == 1);
    CHECK(score.tempoMap.entries().front().tick == 0);
}

TEST_CASE("an event reports where it ends", "[score][model]")
{
    ScoreEvent event;
    event.onset = ticksPerQuarterNote;
    event.duration = ticksPerQuarterNote * 2;

    CHECK(endOf(event) == ticksPerQuarterNote * 3);
}

TEST_CASE("a chord is one event carrying several pitches", "[score][model]")
{
    // Invariant 2 only means something if a chord does not count as three
    // overlapping events in one voice.
    ScoreEvent chord;
    chord.kind = EventKind::chord;
    chord.duration = ticksPerQuarterNote;
    chord.notes = notesOf(
        {pitchFromSpelling(Step::c, 0, 4), pitchFromSpelling(Step::e, 0, 4),
         pitchFromSpelling(Step::g, 0, 4)});

    CHECK(chord.notes.size() == 3);
    CHECK(isPitched(chord));
    CHECK(endOf(chord) == ticksPerQuarterNote);
}

TEST_CASE("a rest carries no pitch", "[score][model]")
{
    ScoreEvent rest;
    rest.kind = EventKind::rest;
    rest.duration = ticksPerQuarterNote;

    CHECK_FALSE(isPitched(rest));
    CHECK(rest.notes.empty());
}

TEST_CASE("a grace note has zero duration", "[score][model]")
{
    // Invariant 3: only grace notes may be zero-length, and they do not advance
    // the voice cursor.
    ScoreEvent grace;
    grace.kind = EventKind::note;
    grace.isGrace = true;
    grace.duration = 0;
    grace.notes = notesOf({pitchFromSpelling(Step::d, 0, 5)});

    CHECK(grace.isGrace);
    CHECK(endOf(grace) == grace.onset);
}

TEST_CASE("event references compare by all three coordinates", "[score][model]")
{
    // Tie linkage relies on these, and a tie can cross a barline, so the
    // measure index has to be part of the identity.
    const NoteRef reference{1, 0, 2};

    CHECK(reference == NoteRef{1, 0, 2});
    CHECK(reference != NoteRef{2, 0, 2});
    CHECK(reference != NoteRef{1, 1, 2});
    CHECK(reference != NoteRef{1, 0, 3});
}

TEST_CASE("a tie links two events in both directions", "[score][model]")
{
    ScoreEvent start;
    start.kind = EventKind::note;
    start.notes = notesOf({pitchFromSpelling(Step::g, 0, 4)});
    start.notes.front().tiedTo = NoteRef{1, 0, 0};

    ScoreEvent stop;
    stop.kind = EventKind::note;
    stop.notes = notesOf({pitchFromSpelling(Step::g, 0, 4)});
    stop.notes.front().tiedFrom = NoteRef{0, 0, 0};

    REQUIRE(start.notes.front().tiedTo.has_value());
    REQUIRE(stop.notes.front().tiedFrom.has_value());

    // Invariant 4 also requires both ends to sound the same pitch -- a file may
    // legitimately spell them differently either side of a barline.
    CHECK(soundsSameAs(start.notes.front().written, stop.notes.front().written));
}

TEST_CASE("a plain event carries no tuplet ratio", "[score][model]")
{
    const ScoreEvent plain;

    CHECK(isPlainTuplet(plain.tuplet));

    ScoreEvent triplet;
    triplet.tuplet = TupletRatio{3, 2};

    CHECK_FALSE(isPlainTuplet(triplet.tuplet));
}

TEST_CASE("a measure's nominal duration follows its time signature", "[score][model]")
{
    CHECK(nominalTicks(TimeSignature{4, 4}) == ticksPerWholeNote);
    CHECK(nominalTicks(TimeSignature{3, 4}) == ticksPerQuarterNote * 3);
    CHECK(nominalTicks(TimeSignature{6, 8}) == ticksPerQuarterNote * 3);
}

TEST_CASE("most measures change no attributes", "[score][model]")
{
    const Measure measure;

    CHECK(isEmpty(measure.attributes));

    AttributeChanges changed;
    changed.time = TimeSignature{3, 4};

    CHECK_FALSE(isEmpty(changed));

    AttributeChanges clefOnly;
    clefOnly.clefs = {Clef{"F", 4, 0, 2}};

    CHECK_FALSE(isEmpty(clefOnly));
}

TEST_CASE("printed measure numbers are strings, not integers", "[score][model]")
{
    // MusicXML measure numbers are not integers: a pickup is "0", a split bar
    // is "12a", and some exporters leave them blank. Diagnostics quote this so
    // the user can find the bar on the page.
    Measure pickup;
    pickup.printedNumber = "0";
    pickup.isPickup = true;
    pickup.index = 0;

    Measure split;
    split.printedNumber = "12a";
    split.index = 12;

    CHECK(pickup.printedNumber == "0");
    CHECK(pickup.isPickup);
    CHECK(split.printedNumber == "12a");

    // The index is the identity a consumer should use; the printed number is
    // for display, and the two need not agree.
    CHECK(split.index == 12);
}

TEST_CASE("repeat markings are captured but left uninterpreted", "[score][model]")
{
    // Decision 3: the model is as-written. Nothing here expands a repeat, but
    // the data is kept faithfully so playback can derive an order later.
    Measure measure;

    CHECK(isEmpty(measure.repeats));

    measure.repeats.repeatEnd = true;
    measure.repeats.repeatTimes = 2;
    measure.repeats.endingNumbers = {1};
    measure.repeats.jumps = {"D.C. al Fine"};

    CHECK_FALSE(isEmpty(measure.repeats));
    CHECK(measure.repeats.endingNumbers.size() == 1);
    CHECK(measure.repeats.jumps.front() == "D.C. al Fine");
}

TEST_CASE("a diagnostic can be document-level or located", "[score][model]")
{
    Diagnostic documentLevel;
    documentLevel.severity = DiagnosticSeverity::info;
    documentLevel.message = "unrecognised element";

    CHECK_FALSE(hasLocation(documentLevel.location));

    Diagnostic located;
    located.severity = DiagnosticSeverity::repaired;
    located.location.partId = "P1";
    located.location.measureNumber = "12";
    located.location.voice = 1;

    CHECK(hasLocation(located.location));

    // The location quotes the number as printed, not the zero-based index.
    REQUIRE(located.location.measureNumber.has_value());
    CHECK(*located.location.measureNumber == "12");
}

TEST_CASE("unrecognised elements are summarised with a count", "[score][model]")
{
    // Task 7.2: once per element name, not once per occurrence -- a Sibelius
    // export otherwise produces thousands.
    Diagnostic summary;
    summary.elementName = "print";
    summary.occurrences = 2143;

    CHECK(summary.occurrences == 2143);
    CHECK(summary.elementName == "print");
}

TEST_CASE("a lyric syllable carries what the renderer needs to draw it", "[score][model][lyrics]")
{
    // Neither the hyphen after "be-" nor the extender under a melisma is
    // recoverable from the text alone.
    LyricSyllable begin;
    begin.verse = 1;
    begin.position = SyllabicPosition::begin;
    begin.text = "be";

    LyricSyllable melisma;
    melisma.verse = 2;
    melisma.position = SyllabicPosition::single;
    melisma.text = "A";
    melisma.extend = true;

    CHECK(begin.position == SyllabicPosition::begin);
    CHECK_FALSE(begin.extend);
    CHECK(melisma.extend);
    CHECK(melisma.verse == 2);
}

TEST_CASE("a note can carry several verses at once", "[score][model][lyrics]")
{
    ScoreEvent note;
    note.kind = EventKind::note;
    note.notes = notesOf({pitchFromSpelling(Step::c, 0, 4)});
    note.lyrics = {
        LyricSyllable{1, SyllabicPosition::single, "Praise", false},
        LyricSyllable{2, SyllabicPosition::single, "Sing", false}};

    REQUIRE(note.lyrics.size() == 2);
    CHECK(note.lyrics[0].verse == 1);
    CHECK(note.lyrics[1].verse == 2);
}

TEST_CASE("a clef records its octave change", "[score][model]")
{
    // The vocal tenor clef sounds an octave below where it reads, so playback
    // must not ignore this field.
    const Clef tenorVocal{"G", 2, -1, 1};

    CHECK(tenorVocal.octaveChange == -1);

    const Clef bass{"F", 4, 0, 2};

    CHECK(bass.sign == "F");
    CHECK(bass.line == 4);
    CHECK(bass.staff == 2);
}

TEST_CASE("a key signature distinguishes sharps from flats", "[score][model]")
{
    const KeySignature aMajor{3, "major"};
    const KeySignature bFlatMajor{-2, "major"};

    CHECK(aMajor.fifths == 3);
    CHECK(bFlatMajor.fifths == -2);
}

TEST_CASE("a tempo direction carries a rate and a dynamic does not", "[score][model]")
{
    // Dynamics are notation attached to a position, not playback velocity;
    // turning "mf" into a gain is playback's job, not the shared model's.
    Direction tempo;
    tempo.kind = DirectionKind::tempo;
    tempo.text = "Allegro";
    tempo.beatsPerMinute = 132.0;

    Direction dynamic;
    dynamic.kind = DirectionKind::dynamic;
    dynamic.text = "mf";

    CHECK(tempo.beatsPerMinute.has_value());
    CHECK_FALSE(dynamic.beatsPerMinute.has_value());
    CHECK(dynamic.text == "mf");
}

TEST_CASE("a part records every divisions value it declared", "[score][model]")
{
    // Nothing downstream needs these -- everything is rescaled -- but when a
    // duration converts inexactly, what the file actually said is the useful
    // thing to tell the user.
    Part part;
    part.id = "P1";
    part.sourceDivisions = {{0, 480}, {17, 960}};

    REQUIRE(part.sourceDivisions.size() == 2);
    CHECK(part.sourceDivisions[0].divisions == 480);
    CHECK(part.sourceDivisions[1].measureIndex == 17);
}

TEST_CASE("a part keeps the file's own identifier verbatim", "[score][model]")
{
    // #39's session file needs to name a selected part stably across sessions,
    // and the file's id is the only identifier that survives a re-import.
    const Score score = makeTwoBarScore();

    REQUIRE(score.parts.size() == 1);
    CHECK(score.parts.front().id == "P1");
    CHECK(score.parts.front().name == "Soprano");
}

TEST_CASE("score metadata keeps the encoding software", "[score][model]")
{
    // The first thing worth knowing when a file misbehaves, because exporter
    // dialects differ more than the format suggests.
    ScoreMetadata metadata;
    metadata.workTitle = "Wachet auf";
    metadata.composer = "J. S. Bach";
    metadata.encodingSoftware = "MuseScore 4.4.4";

    CHECK(metadata.encodingSoftware == "MuseScore 4.4.4");
    CHECK(metadata.movementTitle.empty());
}
