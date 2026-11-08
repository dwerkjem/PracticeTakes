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
    CHECK(holder.whyNone() == "No score is open");

    auto target = std::make_shared<pitch::Target>();
    target->name = "B3";
    holder.publish(target);
    CHECK(holder.target() == target);
    CHECK(holder.whyNone().empty());

    holder.publish(nullptr, "Every part plays more than one note at a time; a target is one note");
    CHECK(holder.target() == nullptr);
    CHECK(holder.whyNone().find("more than one note") != std::string::npos);
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
    CHECK(why == "Every part plays more than one note at a time; a target is one note");

    CHECK(pitch::targetFromScore(nullptr).second == "No score is open");
}
