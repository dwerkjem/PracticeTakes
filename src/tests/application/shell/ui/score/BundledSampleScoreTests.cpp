#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include "PracticeTakesSamples.h"
#include "application/shell/ui/score/BundledSampleScore.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// The sample a first launch opens. What matters about it is that it is there,
// that it is the file whose licence is on record, and that opening it twice
// does not quietly stop opening the one this build ships.
namespace
{
const void* sampleBytes()
{
    return PracticeTakesSamples::mahlerdiezweiblauenaugen_mxl;
}

std::size_t sampleSize()
{
    return static_cast<std::size_t>(PracticeTakesSamples::mahlerdiezweiblauenaugen_mxlSize);
}

// A fresh folder under the system temporary directory, removed with everything
// in it when the test ends, so no run sees a previous run's sample.
class ScratchFolder
{
  public:
    ScratchFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("practice-takes-sample-test", ""))
    {
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

bool holdsSample(const juce::File& file)
{
    juce::MemoryBlock onDisk;

    return file.loadFileAsData(onDisk) && onDisk == juce::MemoryBlock(sampleBytes(), sampleSize());
}
} // namespace

TEST_CASE("the shipped sample is the corpus file whose licence was checked", "[score][sample]")
{
    // Provenance is recorded once, against the corpus copy. A shipped copy that
    // drifted from it would be a file nobody has checked the licence of.
    const juce::File corpusCopy =
        juce::File(PRACTICE_TAKES_SOURCE_DIR)
            .getChildFile("src/tests/resources/musicxml")
            .getChildFile("mahler-4-die-zwei-blauen-augen-von-meinem-schatz.mxl");

    REQUIRE(corpusCopy.existsAsFile());
    CHECK(holdsSample(corpusCopy));
}

TEST_CASE("the shipped sample imports as the song it is named for", "[score][sample]")
{
    const ScratchFolder scratch;
    const juce::File file = bundled_sample::install(scratch.get(), sampleBytes(), sampleSize());
    REQUIRE(file.existsAsFile());

    const auto result = score::musicxml::importMusicXmlFile(file);

    REQUIRE(score::musicxml::succeeded(result.status));
    REQUIRE(result.score != nullptr);

    const score::Score& song = *result.score;
    CHECK(song.metadata.movementTitle == "Die zwei blauen Augen");
    CHECK(song.metadata.composer == "Gustav Mahler");

    // Voice and piano, the piano on two staves: the sample is chosen to show
    // more than one staff, so a sample that lost its piano would be a
    // different sample.
    REQUIRE(song.parts.size() == 2);
    CHECK(song.parts[0].staffCount == 1);
    CHECK(song.parts[1].staffCount == 2);
    CHECK(song.parts[1].measures.size() == 68);
}

TEST_CASE("installing the sample writes it into a folder it creates", "[score][sample]")
{
    const ScratchFolder scratch;
    const juce::File folder = scratch.get().getChildFile("nested").getChildFile("Samples");
    REQUIRE_FALSE(folder.exists());

    const juce::File file = bundled_sample::install(folder, sampleBytes(), sampleSize());

    CHECK(file == folder.getChildFile(bundled_sample::fileName()));
    CHECK(holdsSample(file));
}

TEST_CASE("an intact copy of the sample is left alone", "[score][sample]")
{
    const ScratchFolder scratch;
    const juce::File first = bundled_sample::install(scratch.get(), sampleBytes(), sampleSize());
    REQUIRE(first.existsAsFile());

    // Backdated, so a rewrite would show as a newer modification time without
    // depending on the filesystem's timestamp resolution.
    const juce::Time backdated(2001, 0, 1, 0, 0);
    REQUIRE(first.setLastModificationTime(backdated));

    const juce::File second = bundled_sample::install(scratch.get(), sampleBytes(), sampleSize());

    CHECK(second == first);
    CHECK(second.getLastModificationTime() == backdated);
}

TEST_CASE("a damaged copy of the sample is replaced", "[score][sample]")
{
    const ScratchFolder scratch;
    const juce::File file = bundled_sample::install(scratch.get(), sampleBytes(), sampleSize());
    REQUIRE(file.existsAsFile());

    SECTION("edited to the same length")
    {
        juce::MemoryBlock edited(sampleBytes(), sampleSize());
        static_cast<char*>(edited.getData())[edited.getSize() / 2] ^= 0x5a;
        REQUIRE(file.replaceWithData(edited.getData(), edited.getSize()));
    }

    SECTION("truncated")
    {
        REQUIRE(file.replaceWithText("not a score"));
    }

    SECTION("deleted")
    {
        REQUIRE(file.deleteFile());
    }

    const juce::File reinstalled =
        bundled_sample::install(scratch.get(), sampleBytes(), sampleSize());

    CHECK(reinstalled == file);
    CHECK(holdsSample(reinstalled));
}

TEST_CASE("a sample that cannot be written says so", "[score][sample]")
{
    const ScratchFolder scratch;
    REQUIRE(scratch.get().createDirectory().wasOk());

    SECTION("a file is where the folder should be")
    {
        const juce::File blocked = scratch.get().getChildFile("Samples");
        REQUIRE(blocked.replaceWithText("in the way"));

        CHECK(bundled_sample::install(blocked, sampleBytes(), sampleSize()) == juce::File());
    }

    SECTION("there are no bytes to write")
    {
        CHECK(bundled_sample::install(scratch.get(), nullptr, 0) == juce::File());
        CHECK_FALSE(scratch.get().getChildFile(bundled_sample::fileName()).exists());
    }
}
