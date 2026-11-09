#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/CurrentTarget.h"

#include <memory>

using namespace score;

namespace
{
[[nodiscard]] Part partOf(std::string name, std::vector<ScoreEvent> events)
{
    Part part;
    part.id = name;
    part.name = std::move(name);
    Measure measure;
    measure.printedNumber = "1";
    measure.nominalDuration = ticksPerWholeNote;
    Voice voice;
    voice.number = 1;
    voice.events = std::move(events);
    measure.voices.push_back(voice);
    part.measures.push_back(measure);
    return part;
}

[[nodiscard]] ScoreEvent eventOf(std::vector<Pitch> pitches)
{
    ScoreEvent event;
    event.kind = pitches.size() > 1 ? EventKind::chord : EventKind::note;
    event.duration = ticksPerWholeNote;
    for (const Pitch& pitch : pitches)
    {
        event.notes.push_back(Note{pitch, pitch, std::nullopt, std::nullopt});
    }
    return event;
}
} // namespace

TEST_CASE(
    "there is no target until one is published, and then a reason or a note",
    "[pitch][target]")
{
    pitch::CurrentTarget holder;
    CHECK(holder.target() == nullptr);
    CHECK(holder.whyNone() == "target.none.no-score");

    auto target = std::make_shared<pitch::Target>();
    target->name = "B3";
    holder.publish(target);
    CHECK(holder.target() == target);
    CHECK(holder.whyNone().empty());

    holder.publish(nullptr, "target.none.polyphonic");
    CHECK(holder.target() == nullptr);
    CHECK(holder.whyNone() == "target.none.polyphonic");
}

TEST_CASE("a song's target is its voice; a piano piece has none", "[pitch][target]")
{
    auto song = std::make_shared<Score>();
    song->parts = {
        partOf(
            "Piano",
            {eventOf({pitchFromSpelling(Step::c, 0, 4), pitchFromSpelling(Step::g, 0, 4)})}),
        partOf("Voice", {eventOf({pitchFromSpelling(Step::b, 0, 3)})})};
    const auto [target, reason] = pitch::targetFromScore(song);
    REQUIRE(target != nullptr);
    CHECK(target->name == "B3");
    CHECK(target->midiNoteNumber == 59);
    CHECK(target->source == "Voice, bar 1");
    CHECK(reason.empty());

    auto piano = std::make_shared<Score>();
    piano->parts = {partOf(
        "Piano", {eventOf({pitchFromSpelling(Step::c, 0, 4), pitchFromSpelling(Step::g, 0, 4)})})};
    const auto [none, why] = pitch::targetFromScore(piano);
    CHECK(none == nullptr);
    CHECK(why == "target.none.polyphonic");

    CHECK(pitch::targetFromScore(nullptr).second == "target.none.no-score");
}

TEST_CASE("a chosen target is kept over a new score only when asked", "[pitch][target]")
{
    pitch::CurrentTarget holder;
    CHECK(holder.scoreReplacesTarget());

    const auto bFlat = pitchFromSpelling(Step::b, -1, 3);
    holder.publish(pitch::chosenTarget(bFlat, bFlat, "Chosen"));
    REQUIRE(holder.target() != nullptr);
    CHECK(holder.target()->origin == pitch::TargetOrigin::chosen);
    CHECK(holder.target()->name == "Bb3");
    CHECK(holder.target()->midiNoteNumber == 58);
    CHECK(holder.scoreReplacesTarget());

    holder.keepChosenWhenScoreOpens = true;
    CHECK_FALSE(holder.scoreReplacesTarget());

    // A target that came from a score is still replaced by the next one.
    auto fromScore = std::make_shared<pitch::Target>();
    holder.publish(fromScore);
    CHECK(holder.scoreReplacesTarget());
}

TEST_CASE("a target chosen from a transposing part keeps both names", "[pitch][target]")
{
    // A B-flat clarinet's written D5 sounds C5.
    const auto target = pitch::chosenTarget(
        pitchFromSpelling(Step::c, 0, 5), pitchFromSpelling(Step::d, 0, 5), "Clarinet, bar 3");
    CHECK(target->name == "C5");
    CHECK(target->writtenName == "D5");
    CHECK_FALSE(target->soundsAsWritten);
}

TEST_CASE("a cleared target is none, and says the player cleared it", "[pitch][target]")
{
    pitch::CurrentTarget target;
    score::Pitch g4;
    g4.step = score::Step::g;
    g4.octave = 4;
    g4.midiNoteNumber = 67;
    target.publish(pitch::chosenTarget(g4, g4, "test"));
    REQUIRE(target.target() != nullptr);

    target.clear();
    CHECK(target.target() == nullptr);
    CHECK(target.whyNone() == "target.none.cleared");
    // Nothing chosen any more, so a score opening replaces it.
    CHECK(target.scoreReplacesTarget());
}
