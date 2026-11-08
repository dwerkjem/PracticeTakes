#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include "../audio/RealtimeSafety.h"
#include "PlaybackEvents.h"

struct tsf;

// The score's sound: TinySoundFont playing a General MIDI SoundFont, wrapped so
// nothing else knows it exists (no JUCE dependency).
// openspec/changes/score-playback/design.md, decision 1.
//
// `load` does everything that allocates: it reads the file, sets a maximum
// number of voices and creates every channel. Once loaded, nothing below the
// audio-thread line allocates, which the RealtimeSanitizer job checks by
// playing notes through the real callback.
namespace transport
{
class ScoreSynth final
{
  public:
    // Voices sounding at once. When all are busy, a released one is taken; if
    // none is released, a new note is dropped rather than allocating.
    static constexpr int maximumVoices = 96;

    // Frames rendered at a time; a longer block renders in pieces.
    static constexpr int renderFrames = 4096;

    // Any thread but the audio thread. Null when the file cannot be read as a
    // SoundFont.
    [[nodiscard]] static std::unique_ptr<ScoreSynth> load(const std::string& path);

    ~ScoreSynth();
    ScoreSynth(const ScoreSynth&) = delete;
    ScoreSynth& operator=(const ScoreSynth&) = delete;

    // --- Audio thread --------------------------------------------------------

    // The rate it renders at. A change releases everything first.
    void setSampleRate(double rate) noexcept PRACTICE_TAKES_NONBLOCKING;

    // A General MIDI program, 0-127, for a channel (one per track).
    void setProgram(int channel, int program) noexcept PRACTICE_TAKES_NONBLOCKING;

    void noteOn(int channel, int key, float velocity) noexcept PRACTICE_TAKES_NONBLOCKING;
    void noteOff(int channel, int key) noexcept PRACTICE_TAKES_NONBLOCKING;

    // Let a channel's notes, or every note, ring out through their release.
    void releaseChannel(int channel) noexcept PRACTICE_TAKES_NONBLOCKING;
    void releaseAll() noexcept PRACTICE_TAKES_NONBLOCKING;

    // Add `frames` of sound, times `gain`, to `outputs` from `offset`: the first
    // two channels left and right, a single channel the two together.
    void
    renderAdding(float* const* outputs, int channels, int offset, int frames, float gain) noexcept
        PRACTICE_TAKES_NONBLOCKING;

    [[nodiscard]] int activeVoices() const noexcept PRACTICE_TAKES_NONBLOCKING;

  private:
    explicit ScoreSynth(tsf* loaded);

    tsf* synth = nullptr;
    double sampleRate = 0.0;

    // Left and right, interleaved, as TinySoundFont renders them.
    std::unique_ptr<std::array<float, 2 * renderFrames>> scratch;
};
} // namespace transport
