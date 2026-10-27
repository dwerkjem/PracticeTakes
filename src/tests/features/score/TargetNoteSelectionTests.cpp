#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/score/TargetNoteSelection.h"

#include "platform/score/PitchNames.h"
#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/ScoreFixtures.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <string>
#include <vector>

using namespace score;
using score::musicxml::importMusicXmlFile;
using score::musicxml::MusicXmlImportResult;
using score::musicxml::succeeded;
using testing::score::notesOf;

namespace
{
[[nodiscard]] ScoreEvent makeNote(Tick onset, Tick duration, Pitch pitch)
{
    ScoreEvent event;
    event.kind = EventKind::note;
    event.onset = onset;
    event.duration = duration;
    event.notes = notesOf({pitch});

    return event;
}

[[nodiscard]] ScoreEvent makeRest(Tick onset, Tick duration)
{
    ScoreEvent event;
    event.kind = EventKind::rest;
    event.onset = onset;
    event.duration = duration;

    return event;
}

[[nodiscard]] Measure
makeMeasure(std::size_t index, std::string printedNumber, std::vector<Voice> voices)
{
    Measure measure;
    measure.printedNumber = std::move(printedNumber);
    measure.index = index;
    measure.nominalDuration = nominalMeasureTicks(4, 4);
    measure.start = static_cast<Tick>(index) * measure.nominalDuration;
    measure.voices = std::move(voices);

    return measure;
}

[[nodiscard]] Voice makeVoice(int number, std::vector<ScoreEvent> events)
{
    Voice voice;
    voice.number = number;
    voice.events = std::move(events);

    return voice;
}

// One part, whatever measures the case needs, 120 BPM unless a case rebuilds
// the tempo map.
[[nodiscard]] std::shared_ptr<const Score> makeScore(std::vector<Measure> measures)
{
    Part part;
    part.id = "P1";
    part.name = "Soprano";
    part.measures = std::move(measures);

    auto score = std::make_shared<Score>();
    score->metadata.workTitle = "A Test Score";
    score->parts = {part};

    return score;
}

// A single whole note filling bar one, which is the shape most cases want.
[[nodiscard]] std::shared_ptr<const Score> makeOneNoteScore(Pitch pitch)
{
    return makeScore(
        {makeMeasure(0, "1", {makeVoice(1, {makeNote(0, ticksPerWholeNote, pitch)})})});
}
} // namespace

TEST_CASE("no score open is its own outcome", "[score][target-note]")
{
    const TargetNoteSelection selection = selectTargetNote(nullptr);

    CHECK(selection.outcome == TargetNoteOutcome::noScoreOpen);
    CHECK(selection.scoreTitle.empty());
    CHECK_FALSE(selection.note.has_value());
}

TEST_CASE("a score that holds no pitched note says so", "[score][target-note]")
{
    SECTION("a score made only of rests")
    {
        const auto score =
            makeScore({makeMeasure(0, "1", {makeVoice(1, {makeRest(0, ticksPerWholeNote)})})});

        const TargetNoteSelection selection = selectTargetNote(score);

        CHECK(selection.outcome == TargetNoteOutcome::noPitchedNote);
        CHECK_FALSE(selection.note.has_value());

        // Still names the score: "this file has no notes" is only a useful
        // sentence if the reader knows which file it is about.
        CHECK(selection.scoreTitle == "A Test Score");
    }

    SECTION("a score with no measures")
    {
        const TargetNoteSelection selection = selectTargetNote(makeScore({}));

        CHECK(selection.outcome == TargetNoteOutcome::noPitchedNote);
    }

    SECTION("a score with no parts")
    {
        auto score = std::make_shared<Score>();
        score->metadata.workTitle = "Empty";

        const TargetNoteSelection selection = selectTargetNote(score);

        CHECK(selection.outcome == TargetNoteOutcome::noPitchedNote);
        CHECK(selection.scoreTitle == "Empty");
    }
}

TEST_CASE("the first pitched note of the first part is the target", "[score][target-note]")
{
    const auto score = makeOneNoteScore(pitchFromSpelling(Step::a, 0, 4));

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    REQUIRE(selection.note.has_value());
    CHECK(selection.note->soundingName == "A4");
    CHECK(selection.note->soundingMidiNoteNumber == 69);
    CHECK(selection.note->partName == "Soprano");
    CHECK(selection.note->partId == "P1");
    CHECK(selection.note->printedBarNumber == "1");
    CHECK(selection.note->voiceNumber == 1);
    CHECK(selection.note->location == NoteRef{0, 0, 0, 0});
}

TEST_CASE("leading rests are skipped", "[score][target-note]")
{
    const auto score = makeScore({
        makeMeasure(0, "1", {makeVoice(1, {makeRest(0, ticksPerWholeNote)})}),
        makeMeasure(
            1, "2",
            {makeVoice(
                1, {makeRest(0, ticksPerQuarterNote), makeNote(
                                                          ticksPerQuarterNote, ticksPerQuarterNote,
                                                          pitchFromSpelling(Step::d, 0, 5))})}),
    });

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    CHECK(selection.note->soundingName == "D5");
    CHECK(selection.note->printedBarNumber == "2");
    CHECK(selection.note->location.measureIndex == 1);
    CHECK(selection.note->location.eventIndex == 1);
}

// A grace note is a zero-duration event taking its time from performance rather
// than from the bar's arithmetic, so it is not a useful first thing to show --
// and a target with no duration is not a target anyone can play against.
TEST_CASE("a leading grace note is skipped", "[score][target-note]")
{
    ScoreEvent grace = makeNote(0, 0, pitchFromSpelling(Step::g, 0, 4));
    grace.isGrace = true;

    const auto score = makeScore({makeMeasure(
        0, "1",
        {makeVoice(
            1, {grace, makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::a, 0, 4))})})});

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    CHECK(selection.note->soundingName == "A4");
    CHECK(selection.note->durationSeconds > 0.0);
}

TEST_CASE("a leading chord reports that the target is one of its notes", "[score][target-note]")
{
    ScoreEvent chord;
    chord.kind = EventKind::chord;
    chord.onset = 0;
    chord.duration = ticksPerWholeNote;
    chord.notes = notesOf(
        {pitchFromSpelling(Step::c, 0, 4), pitchFromSpelling(Step::e, 0, 4),
         pitchFromSpelling(Step::g, 0, 4)});

    const auto score = makeScore({makeMeasure(0, "1", {makeVoice(1, {chord})})});

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    CHECK(selection.note->isChordMember);
    CHECK(selection.note->chordNoteCount == 3);
    CHECK(selection.note->soundingName == "C4");
    CHECK(selection.note->location.noteIndex == 0);
}

TEST_CASE("a single note is not reported as a chord", "[score][target-note]")
{
    const TargetNoteSelection selection =
        selectTargetNote(makeOneNoteScore(pitchFromSpelling(Step::a, 0, 4)));

    REQUIRE(selection.note.has_value());
    CHECK_FALSE(selection.note->isChordMember);
    CHECK(selection.note->chordNoteCount == 1);
}

// The voices of a measure are independent timelines. Taking the first voice
// that happens to hold a note would report the treble entry here, which is a
// bar and a half after the music actually starts.
TEST_CASE("the earliest onset across voices wins", "[score][target-note]")
{
    const auto score = makeScore({makeMeasure(
        0, "1",
        {
            makeVoice(
                1, {makeRest(0, ticksPerQuarterNote * 2),
                    makeNote(
                        ticksPerQuarterNote * 2, ticksPerQuarterNote * 2,
                        pitchFromSpelling(Step::e, 0, 5))}),
            makeVoice(5, {makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::f, 0, 2))}),
        })});

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    CHECK(selection.note->soundingName == "F2");
    CHECK(selection.note->voiceNumber == 5);
    CHECK(selection.note->location.voiceIndex == 1);
}

TEST_CASE("a tie on the same onset goes to the lower voice number", "[score][target-note]")
{
    const auto score = makeScore({makeMeasure(
        0, "1",
        {
            makeVoice(5, {makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::f, 0, 2))}),
            makeVoice(1, {makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::c, 0, 4))}),
        })});

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    CHECK(selection.note->voiceNumber == 1);
    CHECK(selection.note->soundingName == "C4");
}

TEST_CASE("the staff the note sits on is carried", "[score][target-note]")
{
    ScoreEvent event = makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::f, 0, 2));
    event.staff = 2;

    const auto score = makeScore({makeMeasure(0, "1", {makeVoice(5, {event})})});

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.note.has_value());
    CHECK(selection.note->staff == 2);
}

// The sounding pitch is the target, because a target note exists to be matched
// against audio. Showing a clarinet part's written pitch instead would be
// consistently a tone wrong in a way nothing crashes on.
TEST_CASE("a transposing part targets the sounding pitch and shows both", "[score][target-note]")
{
    const Pitch written = pitchFromSpelling(Step::c, 0, 4);
    const Pitch sounding = transposed(written, Transposition{-1, -2, 0, 1});

    ScoreEvent event;
    event.kind = EventKind::note;
    event.duration = ticksPerWholeNote;
    event.notes = {Note{written, sounding, std::nullopt, std::nullopt}};

    const auto score = makeScore({makeMeasure(0, "1", {makeVoice(1, {event})})});

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    CHECK_FALSE(selection.note->soundsAsWritten);
    CHECK(selection.note->soundingName == "Bb3");
    CHECK(selection.note->writtenName == "C4");
    CHECK(selection.note->soundingMidiNoteNumber == sounding.midiNoteNumber);
}

TEST_CASE("a non-transposing part says the note sounds as written", "[score][target-note]")
{
    const TargetNoteSelection selection =
        selectTargetNote(makeOneNoteScore(pitchFromSpelling(Step::a, 0, 4)));

    REQUIRE(selection.note.has_value());
    CHECK(selection.note->soundsAsWritten);
    CHECK(selection.note->writtenName == selection.note->soundingName);
}

// The model keeps measure numbers as strings because they are not integers: a
// pickup is "0" and a split bar is "12a". Keeping that is only worth anything
// if a consumer shows it.
TEST_CASE("the printed measure number is what is reported", "[score][target-note]")
{
    auto measures = std::vector<Measure>{
        makeMeasure(0, "0", {makeVoice(1, {makeRest(0, ticksPerWholeNote)})}),
        makeMeasure(
            1, "12a",
            {makeVoice(1, {makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::b, -1, 4))})}),
    };

    const TargetNoteSelection selection = selectTargetNote(makeScore(std::move(measures)));

    REQUIRE(selection.outcome == TargetNoteOutcome::found);
    CHECK(selection.note->printedBarNumber == "12a");
    CHECK(selection.note->soundingName == "Bb4");
}

TEST_CASE("timing comes from the score's tempo map", "[score][target-note]")
{
    SECTION("a whole note at the default 120 BPM lasts two seconds")
    {
        const TargetNoteSelection selection =
            selectTargetNote(makeOneNoteScore(pitchFromSpelling(Step::a, 0, 4)));

        REQUIRE(selection.note.has_value());
        CHECK(selection.note->startSeconds == Catch::Approx(0.0));
        CHECK(selection.note->durationSeconds == Catch::Approx(2.0));
    }

    SECTION("a declared tempo is used rather than the default")
    {
        Part part;
        part.id = "P1";
        part.name = "Soprano";
        part.measures = {
            makeMeasure(0, "1", {makeVoice(1, {makeRest(0, ticksPerWholeNote)})}),
            makeMeasure(
                1, "2",
                {makeVoice(1, {makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::a, 0, 4))})}),
        };

        auto score = std::make_shared<Score>();
        score->parts = {part};
        score->tempoMap = TempoMap::build({TempoEntry{0, 60.0}});

        const TargetNoteSelection selection = selectTargetNote(score);

        REQUIRE(selection.note.has_value());

        // Four quarter notes per bar at 60 BPM: the second bar starts at four
        // seconds, and the note in it lasts four.
        CHECK(selection.note->startSeconds == Catch::Approx(4.0));
        CHECK(selection.note->durationSeconds == Catch::Approx(4.0));
    }

    SECTION("a tempo change inside the note is accounted for")
    {
        auto score = std::make_shared<Score>();
        Part part;
        part.id = "P1";
        part.measures = {makeMeasure(
            0, "1",
            {makeVoice(1, {makeNote(0, ticksPerWholeNote, pitchFromSpelling(Step::a, 0, 4))})})};
        score->parts = {part};

        // 60 BPM for the first half of the note, 120 for the second: two
        // seconds plus one, which the tick duration converted at the starting
        // tempo alone would get wrong.
        score->tempoMap =
            TempoMap::build({TempoEntry{0, 60.0}, TempoEntry{ticksPerQuarterNote * 2, 120.0}});

        const TargetNoteSelection selection = selectTargetNote(score);

        REQUIRE(selection.note.has_value());
        CHECK(selection.note->durationSeconds == Catch::Approx(3.0));
    }
}

TEST_CASE("the score is named by whatever the file provides", "[score][target-note]")
{
    const auto base = makeOneNoteScore(pitchFromSpelling(Step::a, 0, 4));

    SECTION("the work title when there is one")
    {
        CHECK(selectTargetNote(base).scoreTitle == "A Test Score");
    }

    SECTION("the movement title when there is no work title")
    {
        auto score = std::make_shared<Score>(*base);
        score->metadata.workTitle.clear();
        score->metadata.movementTitle = "Andante";

        CHECK(selectTargetNote(score).scoreTitle == "Andante");
    }

    SECTION("the first part's name when the file names neither")
    {
        auto score = std::make_shared<Score>(*base);
        score->metadata.workTitle.clear();

        CHECK(selectTargetNote(score).scoreTitle == "Soprano");
    }

    SECTION("a stated placeholder when the file names nothing at all")
    {
        auto score = std::make_shared<Score>(*base);
        score->metadata.workTitle.clear();
        score->parts.front().name.clear();

        CHECK(selectTargetNote(score).scoreTitle == "Untitled score");
    }
}

TEST_CASE("an unnamed part is identified by its id", "[score][target-note]")
{
    auto score = std::make_shared<Score>(*makeOneNoteScore(pitchFromSpelling(Step::a, 0, 4)));
    score->parts.front().name.clear();

    const TargetNoteSelection selection = selectTargetNote(score);

    REQUIRE(selection.note.has_value());
    CHECK(selection.note->partName == "P1");
}

// Everything above runs against scores assembled by hand, which proves the
// rule. This proves the *accessors*: that walking real importer output to the
// first note works on what the importer actually produces, rather than on what
// a fixture assumed it produces. That difference is the whole reason #187 asked
// for a consumer.
TEST_CASE(
    "the target note of a real imported score is its first note",
    "[score][target-note][corpus]")
{
    const juce::File corpus =
        juce::File(PRACTICE_TAKES_TEST_RESOURCES_DIR).getChildFile("musicxml");

    std::vector<juce::File> files;

    for (const juce::File& candidate : corpus.findChildFiles(juce::File::findFiles, false))
    {
        const juce::String extension = candidate.getFileExtension().toLowerCase();

        if (extension == ".musicxml" || extension == ".xml" || extension == ".mxl")
        {
            files.push_back(candidate);
        }
    }

    if (files.empty())
    {
        // The corpus is licence-checked per file and partly gitignored, so a
        // clone can legitimately have none. Reported rather than passed over:
        // an empty corpus is a gap in the safety net, not a clean result.
        WARN(
            "no corpus score found in "
            << corpus.getFullPathName()
            << ", so no real importer "
               "output was checked.");
        SUCCEED("corpus empty -- skipped");

        return;
    }

    for (const juce::File& file : files)
    {
        INFO("corpus file: " << file.getFileName());

        const MusicXmlImportResult result = importMusicXmlFile(file);
        REQUIRE(succeeded(result.status));
        REQUIRE(result.score != nullptr);

        const TargetNoteSelection selection = selectTargetNote(result.score);

        // Every score in the corpus is real repertoire, so every one of them
        // has a note in it. "No pitched note" here would mean the walk is
        // broken, not that the file is empty.
        REQUIRE(selection.outcome == TargetNoteOutcome::found);
        REQUIRE(selection.note.has_value());

        const TargetNote& target = *selection.note;
        const Part& part = result.score->parts.front();

        CHECK_FALSE(selection.scoreTitle.empty());
        CHECK(target.partId == part.id);
        CHECK(target.startSeconds >= 0.0);
        CHECK(target.durationSeconds > 0.0);

        // The reported location names the note that was reported, rather than
        // being a plausible-looking set of indices.
        REQUIRE(target.location.measureIndex < part.measures.size());
        const Measure& measure = part.measures[target.location.measureIndex];
        CHECK(measure.printedNumber == target.printedBarNumber);

        REQUIRE(target.location.voiceIndex < measure.voices.size());
        const Voice& voice = measure.voices[target.location.voiceIndex];
        CHECK(voice.number == target.voiceNumber);

        REQUIRE(target.location.eventIndex < voice.events.size());
        const ScoreEvent& event = voice.events[target.location.eventIndex];
        REQUIRE(target.location.noteIndex < event.notes.size());
        const Note& note = event.notes[target.location.noteIndex];

        CHECK(spelledName(note.sounding) == target.soundingName);
        CHECK(note.sounding.midiNoteNumber == target.soundingMidiNoteNumber);
        CHECK(event.staff == target.staff);
        CHECK(event.isGrace == false);
        CHECK(isPitched(event));

        // And nothing before it was targetable, which is the property that
        // makes it the *first* note rather than merely a note. Stated as a
        // scan over what the chooser rejected, so this is a check rather than a
        // second copy of the rule.
        for (std::size_t measureIndex = 0; measureIndex < target.location.measureIndex;
             ++measureIndex)
        {
            for (const Voice& earlierVoice : part.measures[measureIndex].voices)
            {
                for (const ScoreEvent& earlier : earlierVoice.events)
                {
                    CHECK_FALSE((isPitched(earlier) && !earlier.isGrace));
                }
            }
        }

        for (const Voice& sameMeasureVoice : measure.voices)
        {
            for (const ScoreEvent& other : sameMeasureVoice.events)
            {
                if (!isPitched(other) || other.isGrace)
                {
                    continue;
                }

                CHECK(other.onset >= event.onset);
            }
        }
    }
}
