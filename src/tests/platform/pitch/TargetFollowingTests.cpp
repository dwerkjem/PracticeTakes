#include <catch2/catch_test_macros.hpp>

#include "platform/pitch/TargetFollowing.h"

#include <memory>
#include <vector>

using namespace pitch;

namespace
{
constexpr score::Tick quarter = score::ticksPerQuarterNote;

[[nodiscard]] score::Pitch pitchOf(score::Step step, int octave, int alter = 0)
{
    score::Pitch result;
    result.step = step;
    result.octave = octave;
    result.alter = alter;
    result.midiNoteNumber = score::midiNoteNumberFor(step, alter, octave);
    return result;
}

// A voice singing E4, F#4, a rest, G4, in quarters.
[[nodiscard]] std::shared_ptr<const score::Score> song()
{
    auto source = std::make_shared<score::Score>();
    source->tempoMap = score::TempoMap::build({{0, 120.0}});
    score::Part part;
    part.id = "P1";
    part.name = "Voice";
    score::Measure measure;
    measure.nominalDuration = 4 * quarter;
    score::Voice voice;
    const std::vector<std::optional<score::Pitch>> notes{
        pitchOf(score::Step::e, 4), pitchOf(score::Step::f, 4, 1), std::nullopt,
        pitchOf(score::Step::g, 4)};
    for (std::size_t beat = 0; beat < notes.size(); ++beat)
    {
        score::ScoreEvent event;
        event.onset = static_cast<score::Tick>(beat) * quarter;
        event.duration = quarter;
        if (notes[beat].has_value())
        {
            event.kind = score::EventKind::note;
            event.notes.push_back(
                score::Note{*notes[beat], *notes[beat], std::nullopt, std::nullopt});
        }
        voice.events.push_back(event);
    }
    measure.voices.push_back(voice);
    part.measures.push_back(measure);
    source->parts.push_back(part);
    return source;
}
} // namespace

TEST_CASE("the target at a tick is the note sung there, from the score", "[pitch][follow]")
{
    const auto source = song();
    const score::PartTimeline timeline = score::timelineOf(*source, 0);
    const auto second = targetAtTick(*source, 0, timeline, 1.5 * quarter);
    REQUIRE(second != nullptr);
    CHECK(second->name == "F#4");
    CHECK(second->origin == TargetOrigin::score);
    CHECK(second->source == "Voice, bar 1");
    // The second note of the bar, which is what the Score View outlines.
    REQUIRE(second->place.has_value());
    CHECK(second->place->partIndex == 0);
    CHECK(second->place->note.eventIndex == 1);

    CHECK(targetAtTick(*source, 0, timeline, 2.5 * quarter) == nullptr);
    CHECK(targetAtTick(*source, 0, timeline, 9.0 * quarter) == nullptr);
}

TEST_CASE("a score's target follows the clock while it plays", "[pitch][follow]")
{
    score::CurrentScore holder;
    holder.publish(song());
    CurrentTarget shared;
    shared.publish(targetFromScore(holder.score()).first);
    REQUIRE(shared.target()->name == "E4");

    TargetFollower follower(holder, shared);

    // Not while stopped.
    CHECK_FALSE(follower.follow(1.5 * quarter, false));
    CHECK(shared.target()->name == "E4");

    CHECK(follower.follow(1.5 * quarter, true));
    CHECK(shared.target()->name == "F#4");

    // The same note again publishes nothing; a rest leaves it.
    CHECK_FALSE(follower.follow(1.8 * quarter, true));
    CHECK_FALSE(follower.follow(2.5 * quarter, true));
    CHECK(shared.target()->name == "F#4");

    CHECK(follower.follow(3.2 * quarter, true));
    CHECK(shared.target()->name == "G4");
}

TEST_CASE("a chosen target is never replaced, and following can be off", "[pitch][follow]")
{
    score::CurrentScore holder;
    holder.publish(song());
    CurrentTarget shared;
    shared.publish(
        chosenTarget(pitchOf(score::Step::a, 4), pitchOf(score::Step::a, 4), "Keyboard"));
    TargetFollower follower(holder, shared);
    CHECK_FALSE(follower.follow(1.5 * quarter, true));
    CHECK(shared.target()->name == "A4");

    shared.publish(targetFromScore(holder.score()).first);
    shared.followScoreWhilePlaying = false;
    CHECK_FALSE(follower.follow(1.5 * quarter, true));
    CHECK(shared.target()->name == "E4");
}
