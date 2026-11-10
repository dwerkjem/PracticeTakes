#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "PracticeTakesSamples.h"
#include "application/shell/ui/score/BundledSampleScore.h"
#include "application/testcontrol/SyntheticTake.h"
#include "platform/library/TakeFiles.h"
#include "platform/library/TakeSaver.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// Which takes are saved, and that each is saved once.
namespace
{
class ScratchFolder
{
  public:
    ScratchFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getNonexistentChildFile("practice-takes-take-saver-test", ""))
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

std::shared_ptr<const take::Take> kept(std::uint64_t id, take::TakeEvent ending)
{
    auto recorded = std::make_shared<take::Take>();
    recorded->id = id;
    static_cast<void>(recorded->lifecycle.apply(take::TakeEvent::performanceStarted));
    static_cast<void>(recorded->lifecycle.apply(ending));
    return recorded;
}
} // namespace

TEST_CASE("only new takes of this session are saved", "[library][take-saver]")
{
    auto loaded = std::make_shared<take::Take>(*kept(3, take::TakeEvent::reachedEnd));
    loaded->libraryFolder = "/library/Takes/old";
    const std::vector<std::shared_ptr<const take::Take>> takes{
        kept(1, take::TakeEvent::reachedEnd), kept(2, take::TakeEvent::stopped), loaded,
        kept(4, take::TakeEvent::reachedEnd)};

    const auto all = library::takesToSave(takes, 0);
    REQUIRE(all.size() == 3);
    CHECK(all[0]->id == 1);
    CHECK(all[1]->id == 2);
    CHECK(all[2]->id == 4);
    // Seen once, never again.
    const auto later = library::takesToSave(takes, 2);
    REQUIRE(later.size() == 1);
    CHECK(later[0]->id == 4);
}

TEST_CASE("a kept take is written once, on the saver's thread", "[library][take-saver]")
{
    const juce::ScopedJuceInitialiser_GUI juceRuntime;
    const ScratchFolder scratch;
    const juce::File song = bundled_sample::install(
        scratch.get().getChildFile("Library/Songs"),
        PracticeTakesSamples::mahlerdiezweiblauenaugen_mxl,
        static_cast<std::size_t>(PracticeTakesSamples::mahlerdiezweiblauenaugen_mxlSize));
    const auto imported = score::musicxml::importMusicXmlFile(song);
    REQUIRE(imported.score != nullptr);

    auto played =
        std::make_shared<take::Take>(testcontrol::syntheticKeyboardTake(imported.score, 1, 2));
    played->id = 1;
    played->scoreFile = song.getFullPathName().toStdString();
    const std::vector<std::shared_ptr<const take::Take>> takes{played};

    const library::Layout layout{scratch.get().getChildFile("Library")};
    library::TakeSaver saver;
    std::vector<library::TakeSaver::Saved> reports;
    saver.onSaved = [&reports](const library::TakeSaver::Saved& saved)
    { reports.push_back(saved); };

    saver.offer(layout, takes);
    saver.finishSaving();
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].error.empty());
    CHECK(reports[0].folder.getParentDirectory() == layout.takes());
    CHECK(library::readTake(reports[0].folder).take != nullptr);

    // Offered again, it is not saved again.
    saver.offer(layout, takes);
    saver.finishSaving();
    CHECK(reports.size() == 1);
    CHECK(layout.takes().getNumberOfChildFiles(juce::File::findDirectories) == 1);
}

TEST_CASE("a take that cannot be saved is reported", "[library][take-saver]")
{
    const juce::ScopedJuceInitialiser_GUI juceRuntime;
    const ScratchFolder scratch;
    // The library's place is taken by a file.
    const juce::File blocked = scratch.get().getChildFile("Library");
    REQUIRE(blocked.replaceWithText("not a folder"));

    library::TakeSaver saver;
    std::vector<library::TakeSaver::Saved> reports;
    saver.onSaved = [&reports](const library::TakeSaver::Saved& saved)
    { reports.push_back(saved); };
    saver.offer(library::Layout{blocked}, {kept(1, take::TakeEvent::reachedEnd)});
    saver.finishSaving();
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].error == "library.take.error.unwritable");
    CHECK(reports[0].folder == juce::File());
}
