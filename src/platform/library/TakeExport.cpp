#include "TakeExport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <memory>

#include "../../application/localisation/LocalisationRules.h"
#include "../take/TakePlayback.h"

namespace
{
constexpr int exportBits = 24;
constexpr int exportChannels = 2;
} // namespace

namespace library
{
RenderedTake
renderTake(const take::Take& recorded, transport::ScoreSynth* synth, int pedalThreshold)
{
    RenderedTake rendered;
    rendered.sampleRate = recorded.sampleRate > 0.0 ? recorded.sampleRate : 44100.0;

    // From where it started -- the count-in left out -- to where it ended.
    const transport::Frame start =
        recorded.table != nullptr
            ? transport::frameOfTick(*recorded.table, static_cast<double>(recorded.startTick))
            : 0;
    const transport::Frame end = std::max(start, recorded.lastFrame);
    const auto length = static_cast<std::size_t>(end - start);
    rendered.left.assign(length, 0.0f);
    rendered.right.assign(length, 0.0f);
    if (length == 0)
    {
        return rendered;
    }

    // The keyboard, block by block, each note at its frame.
    if (synth != nullptr && !recorded.keyboard.empty())
    {
        const auto events =
            take::keyboardPlaybackOf(recorded, rendered.sampleRate, pedalThreshold, 0);
        synth->setSampleRate(rendered.sampleRate);
        synth->setProgram(0, transport::defaultMidiProgram);
        synth->releaseAll();
        std::size_t next = 0;
        // Pressed before the window -- in the count-in -- and still sounding:
        // struck at its start.
        float* outputs[exportChannels] = {rendered.left.data(), rendered.right.data()};
        std::size_t done = 0;
        while (done < length)
        {
            const std::size_t blockEnd = std::min(
                length, done + static_cast<std::size_t>(transport::ScoreSynth::renderFrames));
            std::size_t cursor = done;
            while (next < events.size() &&
                   events[next].frame < start + static_cast<transport::Frame>(blockEnd))
            {
                const auto& event = events[next];
                const auto at =
                    static_cast<std::size_t>(std::max<transport::Frame>(0, event.frame - start));
                if (at > cursor)
                {
                    synth->renderAdding(
                        outputs, exportChannels, static_cast<int>(cursor),
                        static_cast<int>(at - cursor), exportKeyboardGain);
                    cursor = at;
                }
                if (event.on)
                {
                    synth->noteOn(0, event.key, static_cast<float>(event.velocity) / 127.0f);
                }
                else
                {
                    synth->noteOff(0, event.key);
                }
                ++next;
            }
            if (blockEnd > cursor)
            {
                synth->renderAdding(
                    outputs, exportChannels, static_cast<int>(cursor),
                    static_cast<int>(blockEnd - cursor), exportKeyboardGain);
            }
            done = blockEnd;
        }
        synth->releaseAll();
    }

    // The microphone, on both sides, where it was recorded.
    if (!recorded.audio.empty())
    {
        const transport::Frame from = std::max(start, recorded.audioStart);
        const transport::Frame to = std::min(end, recorded.audioEnd());
        for (transport::Frame frame = from; frame < to; ++frame)
        {
            const float sample =
                recorded.audio[static_cast<std::size_t>(frame - recorded.audioStart)] *
                exportMicrophoneGain;
            const auto at = static_cast<std::size_t>(frame - start);
            rendered.left[at] += sample;
            rendered.right[at] += sample;
        }
    }
    return rendered;
}

std::string writeWav(const juce::File& file, const RenderedTake& rendered)
{
    static_cast<void>(file.deleteFile());
    auto output = std::make_unique<juce::FileOutputStream>(file);
    if (!output->openedOk())
    {
        return std::string(l10n::key("library.export.error.unwritable"));
    }
    std::unique_ptr<juce::OutputStream> stream = std::move(output);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer = wav.createWriterFor(
        stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate(rendered.sampleRate)
                    .withNumChannels(exportChannels)
                    .withBitsPerSample(exportBits));
    if (writer == nullptr)
    {
        return std::string(l10n::key("library.export.error.unwritable"));
    }
    const float* channels[exportChannels] = {rendered.left.data(), rendered.right.data()};
    const auto total = std::min(rendered.left.size(), rendered.right.size());
    constexpr std::size_t chunk = 65536;
    for (std::size_t done = 0; done < total; done += chunk)
    {
        const auto count = std::min(chunk, total - done);
        const float* at[exportChannels] = {channels[0] + done, channels[1] + done};
        if (!writer->writeFromFloatArrays(at, exportChannels, static_cast<int>(count)))
        {
            writer.reset();
            static_cast<void>(file.deleteFile());
            return std::string(l10n::key("library.export.error.unwritable"));
        }
    }
    // The WAV writer finishes its header when it is destroyed.
    writer.reset();
    return {};
}

juce::String exportFileName(const juce::String& takeFolderName)
{
    return juce::File::createLegalFileName(takeFolderName) + ".wav";
}
} // namespace library
