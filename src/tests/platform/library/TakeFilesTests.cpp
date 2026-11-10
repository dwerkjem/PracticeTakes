#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <memory>

#include "PracticeTakesSamples.h"
#include "application/shell/ui/score/BundledSampleScore.h"
#include "application/testcontrol/SyntheticTake.h"
#include "platform/evaluation/TakeReview.h"
#include "platform/library/TakeFiles.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// A take saved to its folder and read back is the same take, and judges the
// same: what "a saved take holds everything its review needs" means.
namespace
{
class ScratchFolder
{
  public:
    ScratchFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("practice-takes-take-files-test", ""))
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

struct Song
{
    juce::File file;
    std::shared_ptr<const score::Score> score;
};

Song sample(const ScratchFolder& scratch)
{
    const juce::File file = bundled_sample::install(
        scratch.get().getChildFile("Songs"), PracticeTakesSamples::mahlerdiezweiblauenaugen_mxl,
        static_cast<std::size_t>(PracticeTakesSamples::mahlerdiezweiblauenaugen_mxlSize));
    const auto imported = score::musicxml::importMusicXmlFile(file);
    REQUIRE(imported.score != nullptr);
    return {file, imported.score};
}

void checkSameSummary(const evaluation::NoteSummary& one, const evaluation::NoteSummary& other)
{
    CHECK(one.correct == other.correct);
    CHECK(one.wrong == other.wrong);
    CHECK(one.missed == other.missed);
    CHECK(one.extra == other.extra);
    CHECK(one.inTune == other.inTune);
    CHECK(one.sharp == other.sharp);
    CHECK(one.flat == other.flat);
    CHECK(one.uncertain == other.uncertain);
}

constexpr std::int64_t someTime = 1'789'000'000'000LL;
} // namespace

TEST_CASE("a keyboard take saved and loaded judges the same", "[library][take-files]")
{
    const ScratchFolder scratch;
    const Song song = sample(scratch);
    take::Take played = testcontrol::syntheticKeyboardTake(song.score, 1, 4);
    REQUIRE(take::isKept(played.state()));

    const juce::File folder = scratch.get().getChildFile("Takes/one");
    REQUIRE(library::writeTake(folder, played, song.file, someTime).empty());
    CHECK(folder.getChildFile(library::manifestFileName).existsAsFile());
    CHECK(folder.getChildFile("score.mxl").existsAsFile());
    // Nothing from the microphone, so no audio file.
    CHECK_FALSE(folder.getChildFile(library::microphoneFileName).exists());
    CHECK_FALSE(folder.getChildFile("take.json.partial").exists());

    const library::LoadedTake loaded = library::readTake(folder);
    REQUIRE(loaded.take != nullptr);
    CHECK(loaded.error.empty());
    CHECK(loaded.take->libraryFolder == folder.getFullPathName().toStdString());
    CHECK(loaded.take->state() == played.state());
    CHECK(loaded.take->keyboard.size() == played.keyboard.size());
    REQUIRE(loaded.take->table != nullptr);
    CHECK(loaded.take->table->segmentCount == played.table->segmentCount);
    CHECK(loaded.take->tickAt(played.lastFrame) == played.tickAt(played.lastFrame));

    checkSameSummary(
        evaluation::bestPartFor(*loaded.take).report.summary,
        evaluation::bestPartFor(played).report.summary);
}

TEST_CASE("a sung take keeps its audio to 24 bits", "[library][take-files]")
{
    const ScratchFolder scratch;
    const Song song = sample(scratch);
    take::Take sung = testcontrol::syntheticSungTake(song.score, 0, 4);
    REQUIRE(!sung.audio.empty());

    const juce::File folder = scratch.get().getChildFile("Takes/sung");
    REQUIRE(library::writeTake(folder, sung, song.file, someTime).empty());
    CHECK(folder.getChildFile(library::microphoneFileName).existsAsFile());

    const library::LoadedTake loaded = library::readTake(folder);
    REQUIRE(loaded.take != nullptr);
    REQUIRE(loaded.take->audio.size() == sung.audio.size());
    float worst = 0.0f;
    for (std::size_t index = 0; index < sung.audio.size(); ++index)
    {
        worst = std::max(worst, std::abs(loaded.take->audio[index] - sung.audio[index]));
    }
    CHECK(worst < 1.0f / 4'000'000.0f);
    CHECK(loaded.take->audioStart == sung.audioStart);
    CHECK(loaded.take->referenceHz == sung.referenceHz);

    checkSameSummary(
        evaluation::bestPartFor(*loaded.take).report.summary,
        evaluation::bestPartFor(sung).report.summary);
}

TEST_CASE("a take keeps the score it was played against", "[library][take-files]")
{
    const ScratchFolder scratch;
    const Song song = sample(scratch);
    const take::Take played = testcontrol::syntheticKeyboardTake(song.score, 1, 2);
    const juce::File folder = scratch.get().getChildFile("Takes/kept");
    REQUIRE(library::writeTake(folder, played, song.file, someTime).empty());

    // The song is deleted afterwards; the take still opens.
    REQUIRE(song.file.deleteFile());
    const library::LoadedTake loaded = library::readTake(folder);
    REQUIRE(loaded.take != nullptr);
    CHECK(loaded.take->scoreTitle == played.scoreTitle);
    CHECK(loaded.scoreImport.score != nullptr);
}

TEST_CASE("what cannot be saved or read says why", "[library][take-files]")
{
    const ScratchFolder scratch;
    const Song song = sample(scratch);

    // A cancelled take is not saved.
    take::Take cancelled;
    static_cast<void>(cancelled.lifecycle.apply(take::TakeEvent::cancelled));
    const juce::File none = scratch.get().getChildFile("Takes/cancelled");
    CHECK(
        library::writeTake(none, cancelled, song.file, someTime) == "library.take.error.not-kept");
    CHECK_FALSE(none.exists());

    // A folder that is not a take.
    CHECK(library::readTake(scratch.get()).error == "library.take.error.unreadable");

    // A take whose score copy has gone.
    const take::Take played = testcontrol::syntheticKeyboardTake(song.score, 1, 2);
    const juce::File folder = scratch.get().getChildFile("Takes/no-score");
    REQUIRE(library::writeTake(folder, played, song.file, someTime).empty());
    REQUIRE(folder.getChildFile("score.mxl").deleteFile());
    CHECK(library::readTake(folder).error == "library.take.error.no-score");

    // A take from a newer application.
    const juce::File newer = scratch.get().getChildFile("Takes/newer");
    REQUIRE(newer.createDirectory());
    REQUIRE(newer.getChildFile(library::manifestFileName)
                .replaceWithText(
                    R"({"formatVersion": "9.0", "state": "completed", "sampleRate": 44100})"));
    CHECK(library::readTake(newer).error == "library.take.error.newer");
    CHECK(library::readManifest(newer).error == "library.take.error.newer");
}
