#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/score/Score.h"
#include "platform/transport/ClockRenderer.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace transport;

namespace
{
constexpr double rate = 44100.0;
constexpr Tick quarter = score::ticksPerQuarterNote;

[[nodiscard]] score::Score scoreOf(int measures, score::TimeSignature time, double bpm)
{
    score::Score result;
    result.tempoMap = score::TempoMap::build({{0, bpm}});
    score::Part part;
    Tick start = 0;
    for (int index = 0; index < measures; ++index)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(index);
        measure.start = start;
        if (index == 0)
        {
            measure.attributes.time = time;
        }
        measure.nominalDuration = score::nominalTicks(time);
        start += measure.nominalDuration;
        part.measures.push_back(std::move(measure));
    }
    result.parts.push_back(std::move(part));
    return result;
}

[[nodiscard]] std::unique_ptr<ClockTable>
tableOf(const score::Score& source, int scale = 100, Tick loopStart = 0, Tick loopEnd = 0)
{
    auto table = std::make_unique<ClockTable>();
    buildTable(*table, source, scale, rate, loopStart, loopEnd);
    return table;
}

// Drives a renderer as a callback would, recording what it wrote.
struct Harness
{
    ClockRenderer renderer;
    std::vector<float> left;
    std::vector<float> right;

    // Every rendered sample of the left channel, end to end.
    std::vector<float> recorded;

    void block(int frames, bool withOutput = true)
    {
        left.assign(static_cast<std::size_t>(frames), 0.0f);
        right.assign(static_cast<std::size_t>(frames), 0.0f);
        std::array<float*, 2> channels{left.data(), right.data()};
        RenderBlock render;
        render.outputs = withOutput ? channels.data() : nullptr;
        render.outputChannels = withOutput ? 2 : 0;
        render.frames = frames;
        render.sampleRate = rate;
        renderer.render(render);
        recorded.insert(recorded.end(), left.begin(), left.end());
    }

    void blocks(int count, int frames = 512)
    {
        for (int index = 0; index < count; ++index)
        {
            block(frames);
        }
    }

    void play(int countInBars = 0)
    {
        REQUIRE(renderer.send({ClockCommand::Kind::play, 0, countInBars}));
    }
};

// Where each click starts in a recording: a sample that is non-zero after at
// least a click's length of silence. The first sample of a click is exactly
// zero (sin 0), so a click is found at its second sample and reported one
// earlier.
[[nodiscard]] std::vector<std::size_t> clickStarts(const std::vector<float>& samples)
{
    std::vector<std::size_t> starts;
    std::size_t silentRun = ClockRenderer::maximumClickFrames;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        if (samples[index] != 0.0f)
        {
            if (silentRun >= 1000)
            {
                starts.push_back(index - 1);
            }
            silentRun = 0;
        }
        else
        {
            ++silentRun;
        }
    }
    return starts;
}

// Peak of a click starting at `start`: tells the accented click (louder at
// the same gain? no -- the same envelope) apart by its frequency instead, by
// counting zero crossings over the click.
[[nodiscard]] int zeroCrossings(const std::vector<float>& samples, std::size_t start)
{
    int crossings = 0;
    const std::size_t end = std::min(samples.size(), start + 1200);
    for (std::size_t index = start + 2; index < end; ++index)
    {
        if ((samples[index - 1] < 0.0f) != (samples[index] < 0.0f))
        {
            ++crossings;
        }
    }
    return crossings;
}
} // namespace

TEST_CASE("the renderer publishes a table and has it acknowledged", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(4, {4, 4}, 120.0);
    const auto table = tableOf(source);

    CHECK(harness.renderer.publishAcknowledged());
    REQUIRE(harness.renderer.publish(*table));
    CHECK_FALSE(harness.renderer.publishAcknowledged());

    // A second publish before a block has picked up the first is refused.
    CHECK_FALSE(harness.renderer.publish(*table));

    harness.block(64);
    CHECK(harness.renderer.publishAcknowledged());
    CHECK(harness.renderer.snapshot().generation == 1);
    CHECK(harness.renderer.publish(*table));
    CHECK(harness.renderer.tableForGeneration(1) != nullptr);
    CHECK(harness.renderer.tableForGeneration(2) != nullptr);
    CHECK(harness.renderer.tableForGeneration(3) == nullptr);
}

TEST_CASE("a playing clock advances by the frames it is handed", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(8, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.block(64);
    CHECK(harness.renderer.snapshot().position == 0);

    harness.play();
    harness.blocks(10);
    const auto snapshot = harness.renderer.snapshot();
    CHECK(snapshot.state == ClockState::playing);
    CHECK(snapshot.position == 5120);
    CHECK(snapshot.streamFrame == 64 + 9 * 512);
    CHECK(snapshot.blockStartPosition == 4608);
}

TEST_CASE("each click starts on the frame of its beat", "[transport][renderer]")
{
    Harness harness;
    // 120 bpm: a beat every 22,050 frames. Blocks of 500 put every beat at an
    // odd offset inside a block, and some clicks across a block boundary.
    const auto source = scoreOf(4, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.play();
    for (int index = 0; index < 200; ++index)
    {
        harness.block(500);
    }

    const auto starts = clickStarts(harness.recorded);
    REQUIRE(starts.size() >= 4);
    for (std::size_t beat = 0; beat < 4; ++beat)
    {
        CHECK(starts[beat] == beat * 22050);
    }

    // The downbeat is the higher click.
    CHECK(zeroCrossings(harness.recorded, starts[0]) > zeroCrossings(harness.recorded, starts[1]));
}

TEST_CASE("a click straddling a block boundary is carried across it", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(4, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.play();

    // The first beat is at frame 0; a 100-frame block holds only the start of
    // a 1,323-frame click.
    harness.block(100);
    harness.block(2000);

    std::size_t sounding = 0;
    for (std::size_t index = 0; index < harness.recorded.size(); ++index)
    {
        if (harness.recorded[index] != 0.0f)
        {
            sounding = index;
        }
    }
    CHECK(sounding > 1200);
    CHECK(sounding < 1323);
}

TEST_CASE("the metronome can be turned off without stopping the clock", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(4, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.renderer.setMetronomeEnabled(false);
    harness.play();
    harness.blocks(100);

    CHECK(clickStarts(harness.recorded).empty());
    CHECK(harness.renderer.snapshot().position == 51200);
}

TEST_CASE("the volume scales the click", "[transport][renderer]")
{
    Harness loud;
    Harness quiet;
    const auto source = scoreOf(4, {4, 4}, 120.0);
    REQUIRE(loud.renderer.publish(*tableOf(source)));
    REQUIRE(quiet.renderer.publish(*tableOf(source)));
    loud.renderer.setVolume(1.0f);
    quiet.renderer.setVolume(0.25f);
    loud.play();
    quiet.play();
    loud.blocks(4);
    quiet.blocks(4);

    float loudPeak = 0.0f;
    float quietPeak = 0.0f;
    for (std::size_t index = 0; index < loud.recorded.size(); ++index)
    {
        loudPeak = std::max(loudPeak, std::abs(loud.recorded[index]));
        quietPeak = std::max(quietPeak, std::abs(quiet.recorded[index]));
    }
    CHECK(loudPeak > 0.5f);
    CHECK(quietPeak == loudPeak * 0.25f);
}

TEST_CASE("pause holds the position and play resumes from it", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(8, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.play();
    harness.blocks(10);
    REQUIRE(harness.renderer.send({ClockCommand::Kind::pause}));
    harness.blocks(10);
    CHECK(harness.renderer.snapshot().state == ClockState::paused);
    CHECK(harness.renderer.snapshot().position == 5120);

    harness.play();
    harness.blocks(1);
    CHECK(harness.renderer.snapshot().position == 5632);
}

TEST_CASE("stop returns to the start of the loop, or of the score", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(8, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.play();
    harness.blocks(10);
    REQUIRE(harness.renderer.send({ClockCommand::Kind::stop}));
    harness.block(64);
    CHECK(harness.renderer.snapshot().state == ClockState::stopped);
    CHECK(harness.renderer.snapshot().position == 0);

    REQUIRE(harness.renderer.publish(*tableOf(source, 100, 4 * quarter * 2, 4 * quarter * 4)));
    harness.block(64);
    REQUIRE(harness.renderer.send({ClockCommand::Kind::stop}));
    harness.block(64);
    CHECK(harness.renderer.snapshot().position == 2 * 4 * 22050);
}

TEST_CASE("a seek moves to the tick and is counted once", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(12, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.block(64);

    const Tick barNine = 8 * 4 * quarter;
    REQUIRE(harness.renderer.send({ClockCommand::Kind::seek, barNine}));
    harness.block(64);
    CHECK(harness.renderer.snapshot().position == 8 * 4 * 22050);
    CHECK(harness.renderer.events().seeks == 1);
    harness.blocks(5);
    CHECK(harness.renderer.events().seeks == 1);
}

TEST_CASE("a loop wraps by its exact length and counts each wrap", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(8, {4, 4}, 120.0);
    // Bars 2-3: 88,200 frames each.
    REQUIRE(harness.renderer.publish(*tableOf(source, 100, 4 * quarter, 3 * 4 * quarter)));
    harness.block(64);
    REQUIRE(harness.renderer.send({ClockCommand::Kind::seek, 4 * quarter}));
    harness.play();

    const Frame length = 2 * 4 * 22050;
    const int blocks = static_cast<int>(length * 3 / 512) + 1;
    harness.blocks(blocks);

    CHECK(harness.renderer.events().wraps == 3);
    const Frame expected = 4 * 22050 + (static_cast<Frame>(blocks) * 512 % length);
    CHECK(harness.renderer.snapshot().position == expected);
}

TEST_CASE("the end of the piece stops the clock and is reported once", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(1, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.play();
    harness.blocks(200);

    const auto snapshot = harness.renderer.snapshot();
    CHECK(snapshot.state == ClockState::stopped);
    CHECK(snapshot.position == 0);
    CHECK(harness.renderer.events().ends == 1);
}

TEST_CASE("one bar of 3/4 counts in, then the take begins at zero", "[transport][renderer]")
{
    Harness harness;
    // 60 bpm: a beat is 44,100 frames; the count-in is three of them.
    const auto source = scoreOf(4, {3, 4}, 60.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.renderer.setMetronomeEnabled(false);
    harness.play(1);
    harness.block(512);

    auto snapshot = harness.renderer.snapshot();
    CHECK(snapshot.countingIn);
    CHECK(snapshot.countInTarget == 0);
    CHECK(snapshot.position == -3 * 44100 + 512);

    // Through the count-in and a little past zero.
    const int blocks = (3 * 44100) / 512 + 2;
    harness.blocks(blocks);
    snapshot = harness.renderer.snapshot();
    CHECK_FALSE(snapshot.countingIn);
    CHECK(snapshot.position == static_cast<Frame>(blocks + 1) * 512 - 3 * 44100);

    // Three clicks, the first accented, a beat apart; the metronome is off, so
    // nothing sounds at zero.
    const auto starts = clickStarts(harness.recorded);
    REQUIRE(starts.size() == 3);
    CHECK(starts[0] == 0);
    CHECK(starts[1] == 44100);
    CHECK(starts[2] == 88200);
    CHECK(zeroCrossings(harness.recorded, starts[0]) > zeroCrossings(harness.recorded, starts[1]));
}

TEST_CASE("a scale change while playing continues from the same beat", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(10, {4, 4}, 120.0);
    const auto full = tableOf(source, 100);
    const auto slower = tableOf(source, 70);
    REQUIRE(harness.renderer.publish(*full));
    harness.play();
    harness.blocks(100);

    const Frame before = harness.renderer.snapshot().position;
    const double tickBefore = tickAtFrame(*full, before);
    REQUIRE(harness.renderer.publish(*slower));
    harness.block(1);

    const Frame after = harness.renderer.snapshot().position;
    CHECK(std::abs(tickAtFrame(*slower, after) - tickBefore) < 2.0);
    CHECK(after > before);
}

TEST_CASE("the clock advances with no output to write to", "[transport][renderer]")
{
    Harness harness;
    const auto source = scoreOf(4, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.play();
    for (int index = 0; index < 10; ++index)
    {
        harness.block(512, false);
    }
    CHECK(harness.renderer.snapshot().position == 5120);
}

TEST_CASE("rendering a block against a full segment table", "[.benchmark][transport][renderer]")
{
    // 512 segments, the most a table holds: a tempo change every beat. At 200%
    // of 240 bpm a beat is a sixteenth of a second, so a 512-frame block often
    // holds a click, and the search crosses segment boundaries.
    score::Score source = scoreOf(160, {4, 4}, 240.0);
    std::vector<score::TempoEntry> tempi;
    for (int index = 0; index < 600; ++index)
    {
        tempi.push_back({static_cast<Tick>(index) * quarter, 200.0 + index % 80});
    }
    source.tempoMap = score::TempoMap::build(std::move(tempi));
    // Looping the whole piece, so the clock never reaches the end and stops
    // while the benchmark is still timing it.
    const auto table = tableOf(source, 200, 0, 160 * 4 * quarter);
    REQUIRE(table->segmentCount == maximumSegments);
    REQUIRE(table->loopEnabled);

    Harness harness;
    REQUIRE(harness.renderer.publish(*table));
    harness.play();
    harness.block(512);

    std::vector<float> left(512, 0.0f);
    std::vector<float> right(512, 0.0f);
    std::array<float*, 2> channels{left.data(), right.data()};
    RenderBlock block;
    block.outputs = channels.data();
    block.outputChannels = 2;
    block.frames = 512;
    block.sampleRate = rate;

    BENCHMARK("render 512 frames, 512 segments")
    {
        harness.renderer.render(block);
        return left[0];
    };
}

namespace
{
// Every stamp and sample a renderer has recorded, read the way the recorder
// reads them: a stamp, then exactly its samples.
struct Drained
{
    std::vector<RecordedBlock> blocks;
    std::vector<float> samples;
};

[[nodiscard]] Drained drain(ClockRenderer& renderer)
{
    Drained drained;
    RecordedBlock block;
    while (renderer.popRecordedBlock(block))
    {
        drained.blocks.push_back(block);
        std::vector<float> chunk(block.frames);
        REQUIRE(renderer.readRecording(chunk.data(), chunk.size()) == block.frames);
        drained.samples.insert(drained.samples.end(), chunk.begin(), chunk.end());
    }
    return drained;
}

void blockWithInput(Harness& harness, const std::vector<float>& input, float gain = 1.0f)
{
    RenderBlock render;
    render.input = input.data();
    render.inputGain = gain;
    render.frames = static_cast<int>(input.size());
    render.sampleRate = rate;
    harness.renderer.render(render);
}
} // namespace

TEST_CASE("an armed renderer records the input from the count-in on", "[transport][renderer][take]")
{
    Harness harness;
    // 60 bpm 4/4: a one-bar count-in is 176,400 frames.
    const auto source = scoreOf(4, {4, 4}, 60.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.block(64);

    harness.renderer.armRecording();
    CHECK(harness.renderer.recordingArmed());
    harness.play(1);

    std::vector<float> input(512);
    for (std::size_t index = 0; index < input.size(); ++index)
    {
        input.at(index) = static_cast<float>(index) / 1000.0f;
    }
    blockWithInput(harness, input, 0.5f);
    blockWithInput(harness, input, 0.5f);

    const Drained drained = drain(harness.renderer);
    REQUIRE(drained.blocks.size() == 2);
    CHECK(drained.blocks.at(0).position == -4 * 44100);
    CHECK(drained.blocks.at(0).countingIn);
    CHECK(drained.blocks.at(1).position == -4 * 44100 + 512);
    REQUIRE(drained.samples.size() == 1024);

    // With its gain.
    CHECK(drained.samples.at(10) == input.at(10) * 0.5f);
}

TEST_CASE(
    "recording crosses from the count-in into the music without a gap",
    "[transport][renderer][take]")
{
    Harness harness;
    const auto source = scoreOf(4, {4, 4}, 60.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.block(64);
    harness.renderer.armRecording();
    harness.play(1);

    // A block that straddles the count-in's end is two spans.
    const std::vector<float> input(1000, 0.25f);
    const int blocks = (4 * 44100) / 1000 + 1;
    for (int index = 0; index < blocks; ++index)
    {
        blockWithInput(harness, input);
    }

    const Drained drained = drain(harness.renderer);
    Frame expected = -4 * 44100;
    bool sawMusic = false;
    for (const RecordedBlock& block : drained.blocks)
    {
        CHECK(block.position == expected);
        expected += block.frames;
        sawMusic = sawMusic || !block.countingIn;
        if (block.position >= 0)
        {
            CHECK_FALSE(block.countingIn);
        }
    }
    CHECK(sawMusic);
    CHECK(drained.samples.size() == static_cast<std::size_t>(blocks) * 1000);
    CHECK(harness.renderer.recordingOverflows() == 0);
}

TEST_CASE(
    "with no input, silence is recorded so the timeline stays whole",
    "[transport][renderer][take]")
{
    Harness harness;
    const auto source = scoreOf(4, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.block(64);
    harness.renderer.armRecording();
    harness.play();
    harness.block(3000, false);

    const Drained drained = drain(harness.renderer);
    CHECK(drained.samples.size() == 3000);
    CHECK(std::all_of(
        drained.samples.begin(), drained.samples.end(),
        [](float sample) { return sample == 0.0f; }));
    CHECK(drained.blocks.front().position == 0);
}

TEST_CASE("nothing is recorded while disarmed, paused or stopped", "[transport][renderer][take]")
{
    Harness harness;
    const auto source = scoreOf(4, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.play();
    harness.blocks(4);
    CHECK(drain(harness.renderer).blocks.empty());

    harness.renderer.armRecording();
    REQUIRE(harness.renderer.send({ClockCommand::Kind::pause}));
    harness.blocks(4);
    CHECK(drain(harness.renderer).blocks.empty());

    harness.play();
    harness.blocks(2);
    harness.renderer.disarmRecording();
    harness.blocks(2);
    CHECK(drain(harness.renderer).blocks.size() == 2);
}

TEST_CASE(
    "a recording nobody drains overflows rather than leaving a gap",
    "[transport][renderer][take]")
{
    Harness harness;
    const auto source = scoreOf(64, {4, 4}, 120.0);
    REQUIRE(harness.renderer.publish(*tableOf(source)));
    harness.block(64);
    harness.renderer.armRecording();
    harness.play();

    const std::vector<float> input(4096, 0.1f);
    const auto fits = ClockRenderer::recordingCapacity / input.size();
    for (std::size_t index = 0; index < fits + 4; ++index)
    {
        blockWithInput(harness, input);
    }

    CHECK(harness.renderer.recordingOverflows() > 0);

    // What did go in is whole: every stamp has its samples.
    const Drained drained = drain(harness.renderer);
    CHECK(drained.samples.size() == drained.blocks.size() * input.size());

    // Arming again starts clean.
    harness.renderer.armRecording();
    CHECK(harness.renderer.recordingOverflows() == 0);
}

// --- Playing the score (openspec/changes/archive/2026-11-08-score-playback) --------------------

#include "platform/transport/PlaybackEvents.h"
#include "platform/transport/ScoreSynth.h"

namespace
{
// Two bars of 4/4 at 120 bpm: a whole note in the right hand on bar 1, and one
// in the left hand on bar 2. A beat is 22,050 frames.
[[nodiscard]] score::Score twoHands()
{
    score::Score source = scoreOf(2, score::TimeSignature{4, 4}, 120.0);
    score::Part& piano = source.parts.front();
    piano.staffCount = 2;
    for (int bar = 0; bar < 2; ++bar)
    {
        score::ScoreEvent event;
        event.kind = score::EventKind::note;
        event.duration = 4 * quarter;
        event.staff = bar + 1;
        score::Pitch pitch;
        pitch.midiNoteNumber = bar == 0 ? 72 : 48;
        event.notes.push_back(score::Note{pitch, pitch, std::nullopt, std::nullopt});
        score::Voice voice;
        voice.events.push_back(event);
        piano.measures[static_cast<std::size_t>(bar)].voices.push_back(voice);
    }
    return source;
}

struct Playback
{
    Harness harness;
    std::unique_ptr<ScoreSynth> synth = ScoreSynth::load(PRACTICE_TAKES_TEST_SOUNDFONT);
    score::Score source = twoHands();

    explicit Playback(Tick loopStart = 0, Tick loopEnd = 0)
    {
        REQUIRE(synth != nullptr);
        const auto table = tableOf(source, 100, loopStart, loopEnd);
        PlaybackScore notes;
        playbackEventsOf(source, *table, notes);
        REQUIRE(harness.renderer.publish(*table, notes));
        harness.renderer.setMetronomeEnabled(false);
        harness.renderer.setSynth(synth.get());
        harness.renderer.setAudibleTracks(0b11);
        harness.block(64);
        harness.recorded.clear();
    }

    [[nodiscard]] static float
    peakOf(const std::vector<float>& samples, std::size_t from, std::size_t to)
    {
        float loudest = 0.0f;
        for (std::size_t index = from; index < std::min(to, samples.size()); ++index)
        {
            loudest = std::max(loudest, std::abs(samples[index]));
        }
        return loudest;
    }
};
} // namespace

TEST_CASE(
    "the score sounds with the clock, and not before it plays",
    "[transport][renderer][playback]")
{
    Playback fixture;
    fixture.harness.blocks(20);
    CHECK(Playback::peakOf(fixture.harness.recorded, 0, fixture.harness.recorded.size()) == 0.0f);

    fixture.harness.recorded.clear();
    fixture.harness.play();
    fixture.harness.blocks(40);
    CHECK(Playback::peakOf(fixture.harness.recorded, 0, 20000) > 0.01f);
}

TEST_CASE("a muted hand is silent and the other plays on", "[transport][renderer][playback]")
{
    // Mute the right hand: bar 1 is silent, bar 2's left hand sounds.
    Playback fixture;
    fixture.harness.renderer.setAudibleTracks(0b10);
    fixture.harness.play();
    fixture.harness.blocks(4 * 22050 / 512);
    const std::size_t bar = 4 * 22050;
    CHECK(Playback::peakOf(fixture.harness.recorded, 0, bar - 512) == 0.0f);
    fixture.harness.blocks(40);
    CHECK(Playback::peakOf(fixture.harness.recorded, bar, bar + 20000) > 0.01f);

    // Nothing audible, nothing plays.
    Playback silent;
    silent.harness.renderer.setAudibleTracks(0);
    silent.harness.play();
    silent.harness.blocks(400);
    CHECK(Playback::peakOf(silent.harness.recorded, 0, silent.harness.recorded.size()) == 0.0f);
}

TEST_CASE(
    "the count-in plays no notes, and the music starts after it",
    "[transport][renderer][playback]")
{
    // The count-in's clicks always sound, so the synthesiser's voices say
    // whether any note did.
    Playback fixture;
    fixture.harness.play(1);
    const int countInBlocks = 4 * 22050 / 512;
    for (int block = 0; block < countInBlocks - 2; ++block)
    {
        fixture.harness.block(512);
        REQUIRE(fixture.synth->activeVoices() == 0);
    }
    fixture.harness.blocks(10);
    CHECK(fixture.synth->activeVoices() > 0);
}

TEST_CASE("stopping releases every note", "[transport][renderer][playback]")
{
    Playback fixture;
    fixture.harness.play();
    fixture.harness.blocks(20);
    REQUIRE(fixture.synth->activeVoices() > 0);
    REQUIRE(fixture.harness.renderer.send({ClockCommand::Kind::stop}));
    fixture.harness.blocks(400);
    CHECK(fixture.synth->activeVoices() == 0);
}

TEST_CASE(
    "a loop releases at its end and strikes again at its start",
    "[transport][renderer][playback]")
{
    // Loop the first beat: the whole note in bar 1 is struck each time round
    // and never carried past the beat.
    Playback fixture(0, quarter);
    fixture.harness.play();
    fixture.harness.blocks(5 * 22050 / 512);
    const std::size_t beat = 22050;
    CHECK(Playback::peakOf(fixture.harness.recorded, 2 * beat, 2 * beat + 5000) > 0.01f);
    CHECK(fixture.harness.renderer.events().wraps >= 4);
}
