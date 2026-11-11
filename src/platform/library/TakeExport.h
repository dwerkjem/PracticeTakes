#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

#include "../take/Take.h"
#include "../transport/ScoreSynth.h"

// A take as a sound file of its own (openspec/changes/take-playback,
// decision 6): its microphone, and its keyboard played through the piano the
// score plays with, from where it started to where it ended, in stereo at its
// sample rate.
//
// Rendering runs a synthesiser for the whole take and writing touches the
// disk, so both run on the library's worker, never the message or audio
// thread.
namespace library
{
struct RenderedTake
{
    double sampleRate = 44100.0;
    std::vector<float> left;
    std::vector<float> right;
};

// The levels the export mixes at: the microphone as recorded, the keyboard as
// the score's playback sounds by default.
inline constexpr float exportMicrophoneGain = 1.0f;
inline constexpr float exportKeyboardGain = 0.8f;

// `recorded`, rendered. `synth`, when given, plays its keyboard -- and is the
// caller's, loaded for this and used by nothing else meanwhile. Without one a
// keyboard take renders as silence of its length.
[[nodiscard]] RenderedTake
renderTake(const take::Take& recorded, transport::ScoreSynth* synth, int pedalThreshold);

// Write `rendered` to `file` as a 24-bit stereo WAV, replacing it. Empty on
// success; otherwise why not, as a catalogue identifier.
[[nodiscard]] std::string writeWav(const juce::File& file, const RenderedTake& rendered);

// A name for a take's file: its folder's, which is its time and song.
[[nodiscard]] juce::String exportFileName(const juce::String& takeFolderName);
} // namespace library
