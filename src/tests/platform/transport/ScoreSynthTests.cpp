#include <catch2/catch_test_macros.hpp>

#include "platform/transport/ScoreSynth.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace transport;

namespace
{
// The bank CMake downloads, so these run against what ships.
[[nodiscard]] std::unique_ptr<ScoreSynth> loadBank()
{
    auto synth = ScoreSynth::load(PRACTICE_TAKES_TEST_SOUNDFONT);
    REQUIRE(synth != nullptr);
    return synth;
}

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
    std::array<float*, 2> channels{};

    explicit Stereo(int frames)
        : left(static_cast<std::size_t>(frames)), right(static_cast<std::size_t>(frames))
    {
        channels = {left.data(), right.data()};
    }

    [[nodiscard]] float peak() const
    {
        float loudest = 0.0f;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            loudest = std::max({loudest, std::abs(left[index]), std::abs(right[index])});
        }
        return loudest;
    }
};
} // namespace

TEST_CASE("a file that is not a SoundFont loads as nothing", "[transport][synth]")
{
    CHECK(ScoreSynth::load("/nonexistent/bank.sf2") == nullptr);
    CHECK(ScoreSynth::load(PRACTICE_TAKES_SOURCE_DIR "/README.md") == nullptr);
}

TEST_CASE("a note sounds, and rings out after it is released", "[transport][synth]")
{
    const auto synth = loadBank();
    synth->setSampleRate(48000.0);

    Stereo quiet(1024);
    synth->renderAdding(quiet.channels.data(), 2, 0, 1024, 1.0f);
    CHECK(quiet.peak() == 0.0f);

    synth->noteOn(0, 60, 0.8f);
    CHECK(synth->activeVoices() > 0);
    Stereo sounding(4800);
    synth->renderAdding(sounding.channels.data(), 2, 0, 4800, 1.0f);
    CHECK(sounding.peak() > 0.01f);

    // Released, it fades through its envelope to nothing.
    synth->releaseAll();
    for (int block = 0; block < 200 && synth->activeVoices() > 0; ++block)
    {
        Stereo tail(4800);
        synth->renderAdding(tail.channels.data(), 2, 0, 4800, 1.0f);
    }
    CHECK(synth->activeVoices() == 0);
}

TEST_CASE(
    "a block longer than the scratch renders in pieces, from its offset",
    "[transport][synth]")
{
    const auto synth = loadBank();
    synth->noteOn(3, 64, 0.8f);

    const int frames = ScoreSynth::renderFrames + 1000;
    Stereo out(frames + 100);
    synth->renderAdding(out.channels.data(), 2, 100, frames, 1.0f);
    CHECK(std::all_of(out.left.begin(), out.left.begin() + 100, [](float s) { return s == 0.0f; }));
    const auto tail = std::max_element(
        out.left.begin() + ScoreSynth::renderFrames + 100, out.left.end(),
        [](float a, float b) { return std::abs(a) < std::abs(b); });
    CHECK(std::abs(*tail) > 0.0f);
}

TEST_CASE("one output channel hears both sides", "[transport][synth]")
{
    const auto synth = loadBank();
    synth->noteOn(0, 67, 0.8f);
    std::vector<float> mono(4800);
    float* channels[] = {mono.data()};
    synth->renderAdding(channels, 1, 0, 4800, 1.0f);
    CHECK(*std::max_element(mono.begin(), mono.end()) > 0.01f);
}

TEST_CASE("a channel is released alone, and a new rate ends everything", "[transport][synth]")
{
    const auto synth = loadBank();
    synth->setProgram(1, 40);
    synth->noteOn(0, 60, 0.8f);
    synth->noteOn(1, 76, 0.8f);
    const int both = synth->activeVoices();
    REQUIRE(both >= 2);

    synth->releaseChannel(1);
    for (int block = 0; block < 200; ++block)
    {
        Stereo tail(4800);
        synth->renderAdding(tail.channels.data(), 2, 0, 4800, 1.0f);
    }
    CHECK(synth->activeVoices() > 0);
    CHECK(synth->activeVoices() < both);

    synth->setSampleRate(96000.0);
    Stereo after(4800);
    synth->renderAdding(after.channels.data(), 2, 0, 4800, 1.0f);
    CHECK(synth->activeVoices() == 0);
}
