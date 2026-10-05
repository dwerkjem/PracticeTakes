#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/score/ScoreImportState.h"

#include "platform/score/Score.h"

using score::Measure;
using score::Part;
using score::Score;
using score::musicxml::MusicXmlImportResult;
using score::musicxml::MusicXmlImportStatus;

namespace
{
// A one-bar score whose title identifies it, so a test can tell which import's
// score is being held.
[[nodiscard]] MusicXmlImportResult importOf(const std::string& workTitle)
{
    Measure measure;
    measure.printedNumber = "1";
    measure.nominalDuration = score::nominalMeasureTicks(4, 4);

    Part part;
    part.id = "P1";
    part.name = "Piano";
    part.staffCount = 2;
    part.measures = {measure};

    Score score;
    score.metadata.workTitle = workTitle;
    score.parts = {part};

    MusicXmlImportResult result;
    result.status = MusicXmlImportStatus::imported;
    result.score = std::make_shared<const Score>(std::move(score));

    return result;
}

[[nodiscard]] MusicXmlImportResult failureOf(const MusicXmlImportStatus status, std::string error)
{
    MusicXmlImportResult result;
    result.status = status;
    result.error = std::move(error);

    return result;
}
} // namespace

TEST_CASE("no score is held before the first import", "[score][import][state]")
{
    const ScoreImportState state;

    CHECK_FALSE(state.hasScore());
    CHECK(state.score() == nullptr);
    CHECK_FALSE(state.hasImported());
}

TEST_CASE("a successful import becomes the current score", "[score][import][state]")
{
    ScoreImportState state;

    const ScoreImportSummary& summary = state.apply(importOf("Gymnopédie"));

    REQUIRE(state.hasScore());
    CHECK(state.score()->metadata.workTitle == "Gymnopédie");
    CHECK(state.hasImported());
    CHECK(summary.succeeded);
    CHECK(state.summary().succeeded);
}

TEST_CASE("a later successful import replaces the current score", "[score][import][state]")
{
    ScoreImportState state;
    state.apply(importOf("First"));
    state.apply(importOf("Second"));

    REQUIRE(state.hasScore());
    CHECK(state.score()->metadata.workTitle == "Second");
}

TEST_CASE("a failed import leaves the current score open", "[score][import][state]")
{
    ScoreImportState state;
    state.apply(importOf("Gymnopédie"));

    state.apply(failureOf(MusicXmlImportStatus::malformedXml, "Unexpected end of document"));

    // Closing the score a user already has open because a *second* file turned
    // out to be malformed would be a worse bug than the malformed file.
    REQUIRE(state.hasScore());
    CHECK(state.score()->metadata.workTitle == "Gymnopédie");

    // The summary still reports the failure, so the user is told.
    CHECK_FALSE(state.summary().succeeded);
    CHECK(state.summary().detail == "Unexpected end of document");
}

TEST_CASE("a failed first import leaves no score and still reports", "[score][import][state]")
{
    ScoreImportState state;

    state.apply(failureOf(MusicXmlImportStatus::notMusicXml, "No <score-partwise> element"));

    CHECK_FALSE(state.hasScore());
    CHECK(state.score() == nullptr);
    CHECK(state.hasImported());
    CHECK_FALSE(state.summary().succeeded);
}

TEST_CASE("a reader's score survives a later import", "[score][import][state]")
{
    ScoreImportState state;
    state.apply(importOf("First"));

    // A reader takes the score by value, the way the score tool and the
    // renderer will.
    const std::shared_ptr<const Score> held = state.score();

    state.apply(importOf("Second"));

    // The reader's score is neither swapped underneath it nor destroyed.
    REQUIRE(held != nullptr);
    CHECK(held->metadata.workTitle == "First");
    CHECK(state.score()->metadata.workTitle == "Second");
    CHECK(held != state.score());
}
