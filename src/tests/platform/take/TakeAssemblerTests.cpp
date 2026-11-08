#include <catch2/catch_test_macros.hpp>

#include "platform/take/TakeAssembler.h"

#include <vector>

using namespace take;

TEST_CASE("contiguous blocks make one run of audio", "[take][assembler]")
{
    Take target;
    const std::vector<float> first(100, 0.25f);
    const std::vector<float> second(50, 0.5f);

    CHECK(appendBlock(target, {-100, 100, true}, first.data()) == AssemblyResult::appended);
    CHECK(appendBlock(target, {0, 50, false}, second.data()) == AssemblyResult::appended);

    CHECK(target.audioStart == -100);
    CHECK(target.audioEnd() == 50);
    REQUIRE(target.audio.size() == 150);
    CHECK(target.audio.at(99) == 0.25f);
    CHECK(target.audio.at(100) == 0.5f);
    CHECK(target.countInFrames == 100);
}

TEST_CASE("a block that does not continue the audio is reported", "[take][assembler]")
{
    Take target;
    const std::vector<float> block(64, 0.1f);
    REQUIRE(appendBlock(target, {0, 64, false}, block.data()) == AssemblyResult::appended);

    // A gap, and an overlap: neither is appended.
    CHECK(appendBlock(target, {128, 64, false}, block.data()) == AssemblyResult::discontinuity);
    CHECK(appendBlock(target, {32, 64, false}, block.data()) == AssemblyResult::discontinuity);
    CHECK(target.audio.size() == 64);

    // An empty block changes nothing.
    CHECK(appendBlock(target, {999, 0, false}, nullptr) == AssemblyResult::appended);
    CHECK(target.audio.size() == 64);
}

TEST_CASE("a take turns frames into ticks with the table it was recorded at", "[take]")
{
    score::Score source;
    source.tempoMap = score::TempoMap::build({{0, 120.0}});
    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(*table, source.tempoMap, {transport::MeterChange{}}, 0, 50, 44100.0);

    Take target;
    target.table = table;
    target.sampleRate = 44100.0;
    target.lastFrame = 88200;

    // At 50% a quarter at 120 bpm lasts a second.
    CHECK(target.tickAt(44100) == static_cast<double>(score::ticksPerQuarterNote));
    CHECK(target.performedSeconds() == 2.0);

    transport::ClockTiming timing;
    timing.position = 1000;
    timing.timeMilliseconds = 5000.0;
    timing.sampleRate = 44100.0;
    CHECK(frameAtSeconds(timing, 5.01) == 1000 + 441);
}
