#include "TakeFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <utility>

#include "../score/musicxml/MusicXmlImporter.h"

namespace
{
// How many samples are moved through the writer and the reader at a time.
constexpr int chunkSamples = 16384;

// Takes are mono: the analysis tools' signal.
constexpr int channels = 1;

std::string fail(const juce::File& folder, std::string_view error)
{
    folder.deleteRecursively();
    return std::string(error);
}

bool writeMicrophone(const juce::File& file, const take::Take& recorded)
{
    juce::FlacAudioFormat flac;
    auto output = std::make_unique<juce::FileOutputStream>(file);
    if (!output->openedOk())
    {
        return false;
    }
    std::unique_ptr<juce::OutputStream> stream = std::move(output);
    const auto options =
        juce::AudioFormatWriterOptions{}
            .withSampleRate(recorded.sampleRate)
            .withNumChannels(channels)
            .withBitsPerSample(library::microphoneBits);
    std::unique_ptr<juce::AudioFormatWriter> writer = flac.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        return false;
    }

    const float* samples = recorded.audio.data();
    const auto total = recorded.audio.size();
    for (std::size_t done = 0; done < total;)
    {
        const auto count = std::min<std::size_t>(chunkSamples, total - done);
        const float* chunk = samples + done;
        if (!writer->writeFromFloatArrays(&chunk, channels, static_cast<int>(count)))
        {
            return false;
        }
        done += count;
    }
    // The FLAC writer finishes the file -- its header's length and checksum --
    // when it is destroyed; its flush() does nothing and says false.
    writer.reset();
    return file.getSize() > 0;
}

bool readMicrophone(const juce::File& file, std::vector<float>& into)
{
    juce::FlacAudioFormat flac;
    const std::unique_ptr<juce::AudioFormatReader> reader(
        flac.createReaderFor(file.createInputStream().release(), true));
    if (reader == nullptr || reader->lengthInSamples < 0)
    {
        return false;
    }
    into.assign(static_cast<std::size_t>(reader->lengthInSamples), 0.0f);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), chunkSamples);
    for (juce::int64 done = 0; done < reader->lengthInSamples;)
    {
        const auto count =
            static_cast<int>(std::min<juce::int64>(chunkSamples, reader->lengthInSamples - done));
        if (!reader->read(&buffer, 0, count, done, true, false))
        {
            return false;
        }
        std::copy_n(buffer.getReadPointer(0), count, into.begin() + done);
        done += count;
    }
    return true;
}
} // namespace

namespace library
{
std::string writeTake(
    const juce::File& folder,
    const take::Take& recorded,
    const juce::File& scoreSource,
    std::int64_t recordedMs)
{
    if (!take::isKept(recorded.state()))
    {
        return std::string(l10n::key("library.take.error.not-kept"));
    }
    if (!folder.createDirectory())
    {
        return std::string(l10n::key("library.take.error.unwritable"));
    }

    std::string scoreFile;
    if (scoreSource.existsAsFile())
    {
        const juce::String extension =
            scoreSource.hasFileExtension("mxl") ? juce::String(".mxl") : juce::String(".musicxml");
        scoreFile = ("score" + extension).toStdString();
        if (!scoreSource.copyFileTo(folder.getChildFile(scoreFile)))
        {
            return fail(folder, l10n::key("library.take.error.unwritable"));
        }
    }

    if (recorded.usedMicrophone && !recorded.audio.empty() &&
        !writeMicrophone(folder.getChildFile(microphoneFileName), recorded))
    {
        return fail(folder, l10n::key("library.take.error.unwritable"));
    }

    const juce::File partial = folder.getChildFile(juce::String(manifestFileName) + ".partial");
    const std::string manifest = encode(manifestOf(recorded, recordedMs, scoreFile));
    if (!partial.replaceWithData(manifest.data(), manifest.size()) ||
        !partial.moveFileTo(folder.getChildFile(manifestFileName)))
    {
        return fail(folder, l10n::key("library.take.error.unwritable"));
    }
    return {};
}

DecodedManifest readManifest(const juce::File& folder)
{
    const juce::File file = folder.getChildFile(manifestFileName);
    if (!file.existsAsFile())
    {
        return {std::nullopt, std::string(l10n::key("library.take.error.unreadable"))};
    }
    if (file.getSize() > maximumManifestBytes)
    {
        return {std::nullopt, std::string(l10n::key("library.take.error.too-large"))};
    }
    return decode(file.loadFileAsString().toStdString());
}

LoadedTake readTake(const juce::File& folder)
{
    LoadedTake loaded;
    const DecodedManifest decoded = readManifest(folder);
    if (!decoded.manifest.has_value())
    {
        loaded.error = decoded.error;
        return loaded;
    }
    const TakeManifest& manifest = *decoded.manifest;

    // Its own copy of the score, and only that: a take is judged against the
    // score it was played against, whatever became of the song since.
    if (manifest.scoreFile.empty() || !folder.getChildFile(manifest.scoreFile).existsAsFile())
    {
        loaded.error = l10n::key("library.take.error.no-score");
        return loaded;
    }
    loaded.scoreFile = folder.getChildFile(manifest.scoreFile);
    loaded.scoreImport = score::musicxml::importMusicXmlFile(loaded.scoreFile);
    if (loaded.scoreImport.score == nullptr)
    {
        loaded.error = l10n::key("library.take.error.no-score");
        return loaded;
    }

    auto result = std::make_shared<take::Take>();
    applyManifest(manifest, *result);
    result->score = loaded.scoreImport.score;
    result->scoreFile = loaded.scoreFile.getFullPathName().toStdString();
    result->libraryFolder = folder.getFullPathName().toStdString();

    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(
        *table, *result->score, manifest.scalePercent, manifest.sampleRate, manifest.loopStartTick,
        manifest.loopEndTick);
    result->table = std::move(table);

    if (manifest.usedMicrophone)
    {
        const juce::File audio = folder.getChildFile(microphoneFileName);
        if (audio.existsAsFile() && !readMicrophone(audio, result->audio))
        {
            loaded.error = l10n::key("library.take.error.no-audio");
            return loaded;
        }
    }

    loaded.take = std::move(result);
    return loaded;
}
} // namespace library
