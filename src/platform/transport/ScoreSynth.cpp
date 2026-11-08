#include "ScoreSynth.h"

#include <algorithm>
#include <new>

// TinySoundFont is compiled here and nowhere else.
#define TSF_IMPLEMENTATION
#include <tsf.h>

// The calls below into TinySoundFont are not annotated non-blocking, and some
// of them contain an allocating branch. `load` makes those branches
// unreachable (decision 1); the RealtimeSanitizer job proves it at run time,
// which is the check that counts, so the static warning is quietened here.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif

namespace transport
{
std::unique_ptr<ScoreSynth> ScoreSynth::load(const std::string& path)
{
    tsf* loaded = tsf_load_filename(path.c_str());
    if (loaded == nullptr)
    {
        return nullptr;
    }

    // Everything that could allocate later, now: the voices, and a channel per
    // track, each set to a program so it exists.
    if (tsf_set_max_voices(loaded, maximumVoices) == 0)
    {
        tsf_close(loaded);
        return nullptr;
    }
    for (int channel = 0; channel < static_cast<int>(maximumPlaybackTracks); ++channel)
    {
        if (tsf_channel_set_presetnumber(loaded, channel, defaultMidiProgram) == 0)
        {
            tsf_close(loaded);
            return nullptr;
        }
    }
    tsf_set_output(loaded, TSF_STEREO_INTERLEAVED, 44100, 0.0f);

    std::unique_ptr<ScoreSynth> synth(new (std::nothrow) ScoreSynth(loaded));
    if (synth == nullptr)
    {
        tsf_close(loaded);
        return nullptr;
    }
    synth->sampleRate = 44100.0;
    synth->scratch = std::make_unique<std::array<float, 2 * renderFrames>>();
    return synth;
}

ScoreSynth::ScoreSynth(tsf* loaded) : synth(loaded) {}

ScoreSynth::~ScoreSynth()
{
    tsf_close(synth);
}

void ScoreSynth::setSampleRate(double rate) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (rate <= 0.0 || rate == sampleRate)
    {
        return;
    }
    // Not tsf_reset, which frees the channels: every note ends at once.
    for (int channel = 0; channel < static_cast<int>(maximumPlaybackTracks); ++channel)
    {
        tsf_channel_sounds_off_all(synth, channel);
    }
    tsf_set_output(synth, TSF_STEREO_INTERLEAVED, static_cast<int>(rate), 0.0f);
    sampleRate = rate;
}

void ScoreSynth::setProgram(int channel, int program) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (channel < 0 || channel >= static_cast<int>(maximumPlaybackTracks))
    {
        return;
    }
    // A program the bank lacks falls back to the piano rather than to silence.
    if (tsf_channel_set_presetnumber(synth, channel, std::clamp(program, 0, 127)) == 0)
    {
        tsf_channel_set_presetnumber(synth, channel, defaultMidiProgram);
    }
}

void ScoreSynth::noteOn(int channel, int key, float velocity) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (channel >= 0 && channel < static_cast<int>(maximumPlaybackTracks))
    {
        tsf_channel_note_on(synth, channel, key, velocity);
    }
}

void ScoreSynth::noteOff(int channel, int key) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (channel >= 0 && channel < static_cast<int>(maximumPlaybackTracks))
    {
        tsf_channel_note_off(synth, channel, key);
    }
}

void ScoreSynth::releaseChannel(int channel) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (channel >= 0 && channel < static_cast<int>(maximumPlaybackTracks))
    {
        tsf_channel_note_off_all(synth, channel);
    }
}

void ScoreSynth::releaseAll() noexcept PRACTICE_TAKES_NONBLOCKING
{
    tsf_note_off_all(synth);
}

void ScoreSynth::renderAdding(
    float* const* outputs,
    int channels,
    int offset,
    int frames,
    float gain) noexcept PRACTICE_TAKES_NONBLOCKING
{
    float* buffer = scratch->data();
    while (frames > 0)
    {
        const int chunk = std::min(frames, renderFrames);
        tsf_render_float(synth, buffer, chunk, 0);
        if (outputs != nullptr && channels > 0)
        {
            for (int frame = 0; frame < chunk; ++frame)
            {
                const float left = buffer[2 * frame] * gain;
                const float right = buffer[(2 * frame) + 1] * gain;
                if (channels == 1)
                {
                    outputs[0][offset + frame] += 0.5f * (left + right);
                }
                else
                {
                    outputs[0][offset + frame] += left;
                    outputs[1][offset + frame] += right;
                }
            }
        }
        offset += chunk;
        frames -= chunk;
    }
}

int ScoreSynth::activeVoices() const noexcept PRACTICE_TAKES_NONBLOCKING
{
    return tsf_active_voice_count(synth);
}
} // namespace transport
