#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../pitch/IntonationZone.h"
#include "PitchContour.h"

// How in tune one sung note was: its frames split into attack, sustain and
// release, and the sure sustain frames judged against the written pitch (no
// JUCE dependency). openspec/changes/archive/2026-11-09-sung-pitch-judging/design.md, decisions
// 2-4.
namespace evaluation
{
struct IntonationSettings
{
    // In tune within this many cents either way; the singer's setting.
    double toleranceCents = 25.0;

    // Further than this from the written pitch is nearer another note.
    double wrongCents = 50.0;

    // A frame at least this confident, and voiced, is sure.
    double sureConfidence = 0.8;

    // Fewer sure sustain frames than this, or a smaller share of them, and the
    // note is uncertain.
    std::size_t minimumSureFrames = 4;
    double minimumSureShare = 0.4;

    // The attack: this fraction of the note, clamped.
    double attackFraction = 0.25;
    double minimumAttackMs = 40.0;
    double maximumAttackMs = 150.0;

    // The release: this fraction of the note, at most this long.
    double releaseFraction = 1.0 / 6.0;
    double maximumReleaseMs = 100.0;

    // Where an extra note's pitch is read, after its onset.
    double extraFromMs = 40.0;
    double extraToMs = 200.0;

    [[nodiscard]] bool operator==(const IntonationSettings&) const = default;
};

inline constexpr double minimumToleranceCents = 5.0;
inline constexpr double maximumToleranceCents = 50.0;
inline constexpr double defaultToleranceCents = 25.0;

enum class IntonationVerdict
{
    inTune,
    sharp,
    flat,
    // Nearer another note than the written one.
    wrong,
    // Too few sure frames to say.
    uncertain
};

enum class NoteRegion
{
    attack,
    sustain,
    release
};

struct IntonationFrame
{
    std::int64_t frame = 0;
    NoteRegion region = NoteRegion::sustain;

    // Against the written pitch, folded into -600 to +600 by whole octaves.
    // Zero when unvoiced.
    double cents = 0.0;

    bool voiced = false;
    bool sure = false;
};

struct Intonation
{
    IntonationVerdict verdict = IntonationVerdict::uncertain;

    // The median of the sure sustain frames, folded; positive is sharp.
    double cents = 0.0;

    // Their median absolute deviation about it.
    double spreadCents = 0.0;

    // 0-1: the share of sure sustain frames within the tolerance.
    double score = 0.0;

    // Which zone `cents` is in at the tolerance it was judged with: what the
    // review colours it by. Outside for an uncertain note, which has no cents.
    pitch::IntonationZone zone = pitch::IntonationZone::outside;

    // The octave sung in, against the written one: -1 is an octave low.
    int octave = 0;

    // The nearest note to what was sung, for a wrong note.
    std::optional<int> sungMidi;

    std::size_t sureFrames = 0;
    std::size_t sustainFrames = 0;

    // Every frame of the note, in time order.
    std::vector<IntonationFrame> frames;
};

// Cents from MIDI note `midi` at A4 = `referenceHz`, unfolded.
[[nodiscard]] double centsFrom(double hertz, int midi, double referenceHz) noexcept;

// Fold by whole octaves into [-600, 600), returning the octaves removed.
[[nodiscard]] std::pair<double, int> foldOctaves(double cents) noexcept;

// Judge the note written as `writtenMidi`, sung from clock frame `from` to
// `to`, on `contour`.
[[nodiscard]] Intonation judgeIntonation(
    const PitchContour& contour,
    std::int64_t from,
    std::int64_t to,
    int writtenMidi,
    double referenceHz,
    double sampleRate,
    const IntonationSettings& settings = {});

// The note sung just after an onset at `frame`, if the contour is sure of one.
[[nodiscard]] std::optional<int> pitchAfterOnset(
    const PitchContour& contour,
    std::int64_t frame,
    double referenceHz,
    double sampleRate,
    const IntonationSettings& settings = {});
} // namespace evaluation
