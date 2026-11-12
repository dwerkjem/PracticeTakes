#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "PracticeTakesSamples.h"
#include "application/shell/ui/score/BundledSampleScore.h"
#include "application/testcontrol/SyntheticTake.h"
#include "platform/library/TakeExport.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// A take as a sound file: the keyboard through the piano, the microphone where
// it was recorded, the count-in left out.
namespace
{
class ScratchFolder
{
  public:
    ScratchFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("practice-takes-export-test", ""))
    {
        static_cast<void>(folder.createDirectory());
    }

    ~ScratchFolder()
    {
        folder.deleteRecursively();
    }

    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;

    [[nodiscard]] const juce::File& get() const
    {
        return folder;
    }

  private:
    juce::File folder;
};

std::shared_ptr<const score::Score> sample(const ScratchFolder& scratch)
{
    const juce::File file = bundled_sample::install(
        scratch.get(), PracticeTakesSamples::mahlerdiezweiblauenaugen_mxl,
        static_cast<std::size_t>(PracticeTakesSamples::mahlerdiezweiblauenaugen_mxlSize));
    auto imported = score::musicxml::importMusicXmlFile(file);
    REQUIRE(imported.score != nullptr);
    return imported.score;
}

float peak(const std::vector<float>& samples)
{
    float loudest = 0.0f;
    for (const float value : samples)
    {
        loudest = std::max(loudest, std::abs(value));
    }
    return loudest;
}
} // namespace

TEST_CASE("a keyboard take is exported through the piano", "[library][take-export]")
{
    const ScratchFolder scratch;
    const auto song = sample(scratch);
    const take::Take played = testcontrol::syntheticKeyboardTake(song, 1, 2);
    REQUIRE_FALSE(played.keyboard.empty());

    const auto synth = transport::ScoreSynth::load(PRACTICE_TAKES_TEST_SOUNDFONT);
    REQUIRE(synth != nullptr);
    const library::RenderedTake rendered = library::renderTake(played, synth.get(), 64);
    CHECK(rendered.sampleRate == played.sampleRate);
    REQUIRE(rendered.left.size() == static_cast<std::size_t>(played.lastFrame));
    CHECK(rendered.right.size() == rendered.left.size());
    CHECK(peak(rendered.left) > 0.01f);

    // Without a piano, the same length of silence.
    const library::RenderedTake silent = library::renderTake(played, nullptr, 64);
    CHECK(silent.left.size() == rendered.left.size());
    CHECK(peak(silent.left) == 0.0f);
}

TEST_CASE(
    "a sung take is exported as it was recorded, the count-in left out",
    "[library][take-export]")
{
    const ScratchFolder scratch;
    const auto song = sample(scratch);
    take::Take sung = testcontrol::syntheticSungTake(song, 0, 2);
    REQUIRE_FALSE(sung.audio.empty());
    // A count-in before frame zero, which the export leaves out.
    sung.audioStart = -1000;
    sung.audio.insert(sung.audio.begin(), 1000, 0.9f);

    const library::RenderedTake rendered = library::renderTake(sung, nullptr, 64);
    REQUIRE(rendered.left.size() == static_cast<std::size_t>(sung.lastFrame));
    CHECK(rendered.left[0] == sung.audio[1000]);
    CHECK(rendered.right[500] == sung.audio[1500]);
    CHECK(std::find(rendered.left.begin(), rendered.left.end(), 0.9f) == rendered.left.end());
}

TEST_CASE("an exported take reads back as a stereo WAV of its length", "[library][take-export]")
{
    const ScratchFolder scratch;
    library::RenderedTake rendered;
    rendered.sampleRate = 48000.0;
    rendered.left.assign(4800, 0.25f);
    rendered.right.assign(4800, -0.25f);
    const juce::File file =
        scratch.get().getChildFile(library::exportFileName("2026-11-11 Song: take"));
    CHECK(file.getFileName().endsWith(".wav"));
    CHECK_FALSE(file.getFileName().containsChar(':'));
    REQUIRE(library::writeWav(file, rendered).empty());

    juce::WavAudioFormat wav;
    const std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->numChannels == 2);
    CHECK(reader->sampleRate == 48000.0);
    CHECK(reader->bitsPerSample == 24);
    CHECK(reader->lengthInSamples == 4800);
    juce::AudioBuffer<float> buffer(2, 10);
    REQUIRE(reader->read(&buffer, 0, 10, 0, true, true));
    CHECK(std::abs(buffer.getSample(0, 5) - 0.25f) < 1e-5f);
    CHECK(std::abs(buffer.getSample(1, 5) + 0.25f) < 1e-5f);

    // A folder in the way is said so.
    CHECK(library::writeWav(scratch.get(), rendered) == "library.export.error.unwritable");
}
