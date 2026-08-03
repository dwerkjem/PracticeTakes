#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// Percussion.
//
// The subset excludes it: `<unpitched>` notes have a staff position rather than
// a sounding pitch, and the model has nowhere to put a drum. They import as
// rests with a diagnostic.
//
// **That makes the timing the whole test.** A dropped drum sound is a
// documented limitation; a drum part whose timing is wrong silently shifts
// every other part that shares its bar, and is not a limitation but a bug. It
// was one: requiring a readable `<pitch>` before treating a note as a chord
// tone meant every simultaneous stroke advanced the cursor, so a real
// 32-part arrangement produced 318 truncation repairs from 412 percussion
// chord tones.
//
// No committed corpus score contains percussion -- CPDL is a choral library and
// has none, and the public-domain MuseScore datasets rely on uploader
// self-declaration this project does not accept. So the idiom is reproduced in
// `drumKitDocument`, from a real export, and these are the regression tests.
using namespace score;
using namespace score::musicxml;
using namespace testing::musicxml;

namespace
{
constexpr Tick quarter = ticksPerQuarterNote;
constexpr Tick whole = quarter * 4;

std::shared_ptr<const Score> importOrFail(const std::string& document)
{
    const MusicXmlImportResult result = importMusicXmlDocument(document);

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    return result.score;
}

const Voice& voiceNumbered(const Measure& measure, int number)
{
    for (const Voice& voice : measure.voices)
    {
        if (voice.number == number)
        {
            return voice;
        }
    }

    FAIL("the measure has no voice numbered " << number);

    return measure.voices.front();
}
} // namespace

TEST_CASE("a drum-kit part imports as rests with one diagnostic", "[score][musicxml][percussion]")
{
    const std::string body = measure(
        "1", attributes(1, 4, 4) + drumNote("P1-I36", "F", 4, 1) + drumNote("P1-I38", "C", 5, 1) +
                 drumNote("P1-I36", "F", 4, 1) + drumNote("P1-I38", "C", 5, 1));

    const MusicXmlImportResult result = importMusicXmlDocument(drumKitDocument(body));

    REQUIRE(succeeded(result.status));

    const Voice& voice = result.score->parts.front().measures.front().voices.front();
    REQUIRE(voice.events.size() == 4);

    for (const ScoreEvent& event : voice.events)
    {
        CHECK(event.kind == EventKind::rest);
        CHECK(event.notes.empty());
        CHECK(event.duration == quarter);
    }

    // Reported once for the whole part, not once per stroke.
    const auto reported = std::count_if(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.elementName == "unpitched"; });
    CHECK(reported == 1);
}

TEST_CASE(
    "simultaneous drum strokes consume one duration between them",
    "[score][musicxml][percussion]")
{
    // The regression this file exists for. Four beats, each a kick and a
    // hi-hat struck together: eight <note> elements, four beats of time.
    std::string body = attributes(1, 4, 4);

    for (int beat = 0; beat < 4; ++beat)
    {
        body += drumNote("P1-I42", "G", 5, 1);
        body += drumNote("P1-I36", "F", 4, 1, 1, /*chord=*/true);
    }

    const MusicXmlImportResult result = importMusicXmlDocument(drumKitDocument(measure("1", body)));

    REQUIRE(succeeded(result.status));

    const Measure& bar = result.score->parts.front().measures.front();
    const Voice& voice = bar.voices.front();

    REQUIRE(voice.events.size() == 4);

    for (std::size_t beat = 0; beat < 4; ++beat)
    {
        CHECK(voice.events[beat].onset == quarter * static_cast<Tick>(beat));
        CHECK(voice.events[beat].duration == quarter);
    }

    // The bar is exactly 4/4 -- it did not have to be widened to hold eight
    // quarter notes' worth of over-advanced cursor.
    CHECK(bar.nominalDuration == whole);

    const auto widened = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.elementName == "measure"; });
    CHECK_FALSE(widened);
}

TEST_CASE("a two-voice drum part keeps both voices on the beat", "[score][musicxml][percussion]")
{
    // How a real drum part is written: kick and snare in one voice, hi-hats in
    // another, the second reached by backing the cursor up over the first.
    const std::string body = measure(
        "1", attributes(2, 4, 4) + drumNote("P1-I36", "F", 4, 2) + drumNote("P1-I38", "C", 5, 2) +
                 drumNote("P1-I36", "F", 4, 2) + drumNote("P1-I38", "C", 5, 2) + backup(8) +
                 drumNote("P1-I42", "G", 5, 1, 2) + drumNote("P1-I42", "G", 5, 1, 2) +
                 drumNote("P1-I42", "G", 5, 1, 2) + drumNote("P1-I42", "G", 5, 1, 2) +
                 drumNote("P1-I42", "G", 5, 1, 2) + drumNote("P1-I42", "G", 5, 1, 2) +
                 drumNote("P1-I42", "G", 5, 1, 2) + drumNote("P1-I42", "G", 5, 1, 2));

    const auto score = importOrFail(drumKitDocument(body));
    const Measure& bar = score->parts.front().measures.front();

    REQUIRE(bar.voices.size() == 2);

    const Voice& kit = voiceNumbered(bar, 1);
    REQUIRE(kit.events.size() == 4);

    for (std::size_t beat = 0; beat < 4; ++beat)
    {
        CHECK(kit.events[beat].onset == quarter * static_cast<Tick>(beat));
    }

    // Eight eighth-note hi-hats over the same bar, starting where voice 1 did.
    const Voice& hats = voiceNumbered(bar, 2);
    REQUIRE(hats.events.size() == 8);
    CHECK(hats.events.front().onset == 0);
    CHECK(hats.events.back().onset == quarter * 3 + quarter / 2);
    CHECK(hats.events.back().duration == quarter / 2);

    CHECK(bar.nominalDuration == whole);
}

TEST_CASE(
    "a pitched part alongside a drum part keeps its own timing",
    "[score][musicxml][percussion]")
{
    // The reason drum timing matters even though drum *sound* is dropped:
    // invariant 1 aligns measures across parts, so a drum part whose cursor
    // over-advances drags the bar lines for every other part in the score.
    const std::string drums = measure(
        "1", attributes(1, 4, 4) + drumNote("P1-I42", "G", 5, 1) +
                 drumNote("P1-I36", "F", 4, 1, 1, true) + drumNote("P1-I42", "G", 5, 1) +
                 drumNote("P1-I36", "F", 4, 1, 1, true) + drumNote("P1-I42", "G", 5, 2));
    const std::string voicePart =
        measure("1", attributes(1, 4, 4) + note("C", 5, 2) + note("E", 5, 2));

    const MusicXmlImportResult result =
        importMusicXmlDocument(multiPartDocument({voicePart, drums}));

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score->parts.size() == 2);

    const Measure& sung = result.score->parts[0].measures.front();
    const Measure& kit = result.score->parts[1].measures.front();

    // Both bars are one 4/4 bar long, and they start together.
    CHECK(sung.nominalDuration == whole);
    CHECK(kit.nominalDuration == whole);
    CHECK(sung.start == kit.start);
    CHECK(totalLength(*result.score) == whole);
}

TEST_CASE(
    "an unpitched note stacked on a pitched one does not steal its time",
    "[score][musicxml][percussion]")
{
    // A cue-sized percussion stroke written into a pitched staff. The chord
    // tone contributes no pitch and must contribute no time either.
    const std::string body = measure(
        "1",
        attributes(1, 4, 4) + note("C", 4, 4) + drumNote("P1-I38", "C", 5, 4, 1, /*chord=*/true));

    const auto score = importOrFail(scoreDocument(body));
    const Measure& bar = score->parts.front().measures.front();
    const Voice& voice = bar.voices.front();

    REQUIRE(voice.events.size() == 1);
    CHECK(voice.events.front().duration == whole);
    REQUIRE(voice.events.front().notes.size() == 1);
    CHECK(voice.events.front().notes.front().written.midiNoteNumber == 60);
    CHECK(bar.nominalDuration == whole);
}
