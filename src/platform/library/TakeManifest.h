#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../application/localisation/LocalisationRules.h"
#include "../take/Take.h"

// A saved take's `take.json`: everything about a take but its audio and its
// score, which sit beside it as files (openspec/changes/media-library,
// decisions 3 and 4).
//
// JUCE core only. Encoding and decoding are where every rule about the format
// lives -- its version, what a missing or nonsensical field means -- so they
// are tested here, with no window and no device.
namespace library
{
// The format this build writes. A reader reads every version up to its own
// major and refuses a newer major rather than misread it.
inline constexpr int manifestMajorVersion = 1;
inline constexpr int manifestMinorVersion = 0;

// Larger than any real manifest -- a three-minute keyboard take is a few
// hundred kilobytes -- and small enough that a hostile file cannot exhaust
// memory being parsed.
inline constexpr std::int64_t maximumManifestBytes = 32 * 1024 * 1024;

struct TakeManifest
{
    int majorVersion = manifestMajorVersion;
    int minorVersion = manifestMinorVersion;

    // When it was recorded, as milliseconds since 1970 UTC.
    std::int64_t recordedMs = 0;

    std::string scoreTitle;

    // The score's copy beside the manifest, by file name: `score.mxl` or
    // `score.musicxml`. Empty when the take was saved without one.
    std::string scoreFile;

    score::Tick startTick = 0;
    int scalePercent = transport::defaultScalePercent;
    double sampleRate = 44100.0;
    double referenceHz = 440.0;
    int inputLatencyFrames = 0;
    int outputLatencyFrames = 0;
    bool usedMicrophone = false;
    bool usedKeyboard = false;
    transport::Frame audioStart = 0;
    transport::Frame countInFrames = 0;
    transport::Frame lastFrame = 0;
    std::optional<std::int64_t> stoppedInBar;

    // How it ended: completed, stopped early or failed. Never cancelled, and
    // never live -- neither is saved.
    take::TakeState state = take::TakeState::completed;
    std::string failureReason;

    // The loop the clock played, in ticks; equal when there was none. With the
    // score, scale and sample rate, what rebuilds the take's clock table.
    score::Tick loopStartTick = 0;
    score::Tick loopEndTick = 0;

    std::vector<take::KeyboardEvent> keyboard;

    // How long was played, count-in excluded.
    [[nodiscard]] double performedSeconds() const noexcept
    {
        return sampleRate > 0.0
                   ? static_cast<double>(std::max<transport::Frame>(0, lastFrame)) / sampleRate
                   : 0.0;
    }
};

[[nodiscard]] std::string encode(const TakeManifest& manifest);

struct DecodedManifest
{
    std::optional<TakeManifest> manifest;

    // Why there is none, as a catalogue identifier.
    std::string error;
};

[[nodiscard]] DecodedManifest decode(const std::string& document);

// The manifest of an ended take, recorded at `recordedMs`, with its score
// copied as `scoreFile`.
[[nodiscard]] TakeManifest
manifestOf(const take::Take& recorded, std::int64_t recordedMs, std::string scoreFile);

// A lifecycle that has ended in `state`, by the events that reach it: a loaded
// take's, which is only ever read.
[[nodiscard]] take::TakeLifecycle lifecycleEndingIn(take::TakeState state, std::string reason);

// The manifest's fields onto `onto` -- everything but the score, the clock
// table and the audio, which the caller reads from the files beside it.
void applyManifest(const TakeManifest& manifest, take::Take& onto);
} // namespace library
