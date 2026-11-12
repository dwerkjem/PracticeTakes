#include "../../../localisation/Localisation.h"
#include "../../MainComponent.h"

#include "platform/library/LibraryLayout.h"
#include "platform/library/TakeExport.h"
#include "platform/library/TakeFiles.h"
#include "platform/take/TakePlayback.h"

#include <memory>
#include <utility>

// Hearing a take again (openspec/changes/archive/2026-11-11-take-playback): playing one through
// the clock, and exporting one's audio to a file of the reader's choosing.
//
// Playing is prepared here, on the message thread -- a resample at most, when
// the device runs at another rate than the take was recorded at -- and handed
// to the clock. Exporting renders the whole take through a synthesiser of its
// own, so it runs on the library's worker.

namespace
{
using Safe = juce::Component::SafePointer<MainComponent>;

std::shared_ptr<const take::Take> keptTake(const take::TakeRecorder& recorder, std::uint64_t id)
{
    for (const auto& kept : recorder.takes())
    {
        if (kept != nullptr && kept->id == id)
        {
            return kept;
        }
    }
    return nullptr;
}
} // namespace

bool MainComponent::playTake(std::uint64_t id)
{
    const auto recorded = keptTake(takeRecorder, id);
    // A take plays against the score it was played against, and not over
    // one being recorded.
    if (recorded == nullptr || takeRecorder.isRecording() || recorded->score != scoreImport.score())
    {
        return false;
    }
    take::TakePlaybackMaterial material =
        take::takePlaybackOf(recorded, practiceClock.sampleRate(), midiPedalDownThreshold, 0);

    transport::PracticeClock::Recording recording;
    recording.scalePercent = recorded->scalePercent;
    recording.startTick = recorded->startTick;
    recording.endFrame = material.endFrame;
    recording.keyboard = std::move(material.keyboard);
    recording.sound = transport::TakeSound{
        std::move(material.audioOwner), material.audio, material.audioFrames, material.audioStart};
    return practiceClock.playRecording(std::move(recording));
}

void MainComponent::playLibraryTake(const juce::File& folder)
{
    playLoadedTake = true;
    reviewLibraryTake(folder);
}

void MainComponent::exportTakeAudio(std::uint64_t id)
{
    const auto recorded = keptTake(takeRecorder, id);
    if (recorded == nullptr)
    {
        return;
    }
    // Named as the library names its folder, by when and what.
    const juce::String name =
        recorded->libraryFolder.empty()
            ? library::takeFolderName(
                  juce::Time::getCurrentTime(), juce::String(recorded->scoreTitle))
            : juce::File(juce::String(recorded->libraryFolder)).getFileName();
    exportTakeAudio(recorded, name);
}

void MainComponent::exportLibraryTakeAudio(const juce::File& folder)
{
    const Safe safeThis(this);
    showLibraryStatus(l10n::text("library.take.loading"));
    libraryWork.post(
        [folder, safeThis]() -> library::BackgroundWork::Finish
        {
            auto loaded = std::make_shared<library::LoadedTake>(library::readTake(folder));
            return [safeThis, loaded, folder]
            {
                if (safeThis == nullptr)
                {
                    return;
                }
                if (loaded->take == nullptr)
                {
                    safeThis->showLibraryStatus(l10n::text(loaded->error));
                    return;
                }
                safeThis->showLibraryStatus({});
                safeThis->exportTakeAudio(loaded->take, folder.getFileName());
            };
        });
}

void MainComponent::exportTakeAudio(
    const std::shared_ptr<const take::Take>& recorded,
    const juce::String& name)
{
    if (recorded == nullptr)
    {
        return;
    }
    exportChooser = std::make_unique<juce::FileChooser>(
        l10n::text("take.export.title"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile(library::exportFileName(name)),
        "*.wav", true);
    const Safe safeThis(this);
    exportChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, recorded](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
            {
                return;
            }
            juce::File chosen = chooser.getResult();
            safeThis->exportChooser.reset();
            if (chosen == juce::File())
            {
                return;
            }
            if (!chosen.hasFileExtension("wav"))
            {
                chosen = chosen.withFileExtension("wav");
            }
            const std::string soundFont = safeThis->practiceClock.soundFontLocation();
            const int pedal = safeThis->midiPedalDownThreshold;
            safeThis->libraryWork.post(
                [recorded, chosen, soundFont, pedal, safeThis]() -> library::BackgroundWork::Finish
                {
                    // A piano of its own: the clock's belongs to the audio
                    // thread.
                    const auto synth = recorded->keyboard.empty() || soundFont.empty()
                                           ? nullptr
                                           : transport::ScoreSynth::load(soundFont);
                    const std::string error = library::writeWav(
                        chosen, library::renderTake(*recorded, synth.get(), pedal));
                    return [safeThis, error, chosen]
                    {
                        if (safeThis == nullptr)
                        {
                            return;
                        }
                        if (error.empty())
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::MessageBoxIconType::InfoIcon,
                                l10n::text("take.export.done.title"),
                                l10n::format(
                                    "take.export.done", {{"file", chosen.getFullPathName()}}));
                        }
                        else
                        {
                            juce::AlertWindow::showMessageBoxAsync(
                                juce::MessageBoxIconType::WarningIcon,
                                l10n::text("take.export.failed.title"),
                                l10n::format(
                                    "take.export.failed", {{"file", chosen.getFullPathName()},
                                                           {"reason", l10n::text(error)}}));
                        }
                    };
                });
        });
}
