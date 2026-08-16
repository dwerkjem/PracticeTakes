#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// A signal that looks like somebody singing, for verification captures.
//
// A pure sine is the obvious thing to generate and the wrong one. It draws a
// perfectly flat line on the tuner's graph, a single bar on the harmonic
// analyser, and one clean stripe on the spectrogram -- so every capture shows a
// tool in a state no real input ever produces, and a layout bug that only
// appears when the reading moves is never seen at all.
//
// So this is a sung note rather than a test tone:
//
//   * **Vibrato.** The fundamental is modulated a few cents either side at
//     around five hertz, which is what makes the tuner's needle and graph move
//     instead of sitting still.
//   * **Overtones.** A second and third partial below the fundamental, so the
//     harmonic analyser has more than H1 to draw and the spectrogram has more
//     than one stripe.
//   * **Tremolo and noise.** A slow amplitude sway and a little broadband hiss,
//     so the level meter moves and pitch confidence lands where real input
//     lands rather than at a suspicious 100%.
//
// **Nothing here is heard.** The generator only ever fills a buffer that
// AudioInputService pushes into the analysis FIFOs; the device's output is
// cleared at the top of the callback and never written.
//
// Real-time safe by construction: fixed-size table, no allocation, no locks,
// no branches on shared state, and every trigonometric value read from a table
// built at construction rather than computed per sample.
class SyntheticTone
{
  public:
    // One cycle of a sine. 4096 points is inaudibly fine for this and keeps the
    // table in cache.
    static constexpr std::size_t tableSize = 4096;

    struct Shape
    {
        // Two pitch movements, because the tools disagree about which one they
        // can show.
        //
        // A singer's vibrato is around five hertz, and the tuner deliberately
        // smooths its reading over a dozen analysis frames -- so a fast sway,
        // however wide, averages back into a straight line on the graph. It
        // still moves the meter and the bar, which read the current frame, so
        // it stays.
        //
        // The **drift** is the one the graph can draw: a slow sweep either side
        // of the note, slower than the smoothing, which is what a held note
        // actually does. Forty-five cents is wide enough to see and well inside
        // the semitone, so the note itself never wavers between A4 and its
        // neighbours.
        float vibratoCents = 12.0f;
        float vibratoHz = 5.0f;
        float driftCents = 45.0f;

        // Slow enough to survive the tuner's smoothing (about three quarters of
        // a second), fast enough that a whole sweep fits in the few seconds of
        // history a freshly opened capture has to draw.
        float driftHz = 0.55f;

        // The overtones. Six of them, falling away the way a voice's do, so the
        // harmonic analyser has a spectrum to draw rather than a single bar and
        // the spectrogram has a stack of stripes rather than one line.
        //
        // Two partials was enough to prove the tools were reading something. It
        // was not enough to show what they can do: an analyser with two bars
        // looks the same whether it is working well or barely.
        float secondPartial = 0.34f;
        float thirdPartial = 0.20f;
        float fourthPartial = 0.13f;
        float fifthPartial = 0.09f;
        float sixthPartial = 0.06f;

        // A formant: a band of the spectrum that is louder than the rest, and
        // which moves. This is what a vowel is, and what makes the spectrogram
        // show something travelling rather than a set of parallel lines that
        // could be a still image.
        //
        // Slow -- a full sweep takes about eight seconds -- so a capture with a
        // few seconds of history shows part of a movement rather than a blur,
        // and two captures of the same surface do not look identical.
        float formantHz = 0.12f;

        // Which partial the band sits over at either end of its travel, and how
        // sharply it falls away on each side.
        float formantLowPartial = 1.5f;
        float formantHighPartial = 5.0f;
        float formantWidth = 1.6f;
        float formantDepth = 0.9f;

        // How far the level swells, and how fast.
        //
        // Deep on purpose: a peak, RMS, or loudness meter reading a steady
        // level is as uninformative as a tuner reading a steady pitch, and a
        // gentle sway would leave a loudness graph looking like a ruled line.
        // At 0.55 the note breathes between about a third and full level --
        // roughly eleven decibels peak to trough, which is a singer holding a
        // note rather than a signal generator.
        float tremoloDepth = 0.55f;
        float tremoloHz = 0.45f;

        // Broadband hiss, as a fraction of the peak.
        float noise = 0.012f;
    };

    SyntheticTone()
    {
        for (std::size_t index = 0; index < tableSize; ++index)
        {
            const auto turn = static_cast<double>(index) / static_cast<double>(tableSize);
            table_[index] = static_cast<float>(std::sin(turn * 6.283185307179586));
        }
    }

    // Start again from a known point, so a capture of one surface does not
    // depend on how long the capture of the previous one took.
    void reset() noexcept
    {
        phase_ = 0.0;
        secondPhase_ = 0.0;
        thirdPhase_ = 0.0;
        fourthPhase_ = 0.0;
        fifthPhase_ = 0.0;
        sixthPhase_ = 0.0;
        formantPhase_ = 0.0;
        vibratoPhase_ = 0.0;
        driftPhase_ = 0.0;
        tremoloPhase_ = 0.0;
        noiseState_ = 0x9e3779b9u;
    }

    void setShape(const Shape& shape) noexcept
    {
        shape_ = shape;
    }

    // Fill `destination` with `count` samples of a note at `frequencyHz`.
    //
    // Called on the audio thread. Everything it touches is owned by this
    // object and by that thread alone.
    void render(
        float* destination,
        std::size_t count,
        double frequencyHz,
        double sampleRate,
        float amplitude) noexcept
    {
        if (destination == nullptr || count == 0 || frequencyHz <= 0.0 || sampleRate <= 0.0)
        {
            return;
        }

        const auto turnsPerSample = static_cast<double>(tableSize) / sampleRate;
        const auto vibratoStep = shape_.vibratoHz * turnsPerSample;
        const auto driftStep = shape_.driftHz * turnsPerSample;
        const auto tremoloStep = shape_.tremoloHz * turnsPerSample;
        const auto formantStep = shape_.formantHz * turnsPerSample;
        const auto formantMid =
            0.5 * static_cast<double>(shape_.formantLowPartial + shape_.formantHighPartial);
        const auto formantSwing =
            0.5 * static_cast<double>(shape_.formantHighPartial - shape_.formantLowPartial);

        // A cent is a ratio, so the sway is applied as one -- an even wobble
        // in pitch rather than in hertz, which is what a voice does.
        const auto vibratoRatio = static_cast<double>(shape_.vibratoCents) / 1200.0;
        const auto driftRatio = static_cast<double>(shape_.driftCents) / 1200.0;
        // The swell is folded into the scale as well as the partials, so the
        // loudest sample of the loudest moment still lands on the amplitude
        // that was asked for. Without it, deepening the tremolo would quietly
        // push the signal towards clipping and light up an indicator that has
        // nothing wrong to report.
        //
        // The sum of the partials, with no allowance for the moving band at all.
        //
        // Two corrections that pull opposite ways, and the sum wins: the band
        // lifts a couple of partials at a time, but six partials never peak
        // together -- they are at six different frequencies, so their maxima
        // land at six different moments. Charging the full boost on top of the
        // full sum was doubly pessimistic and quietly halved the signal, which
        // showed up as a tone that no longer reached half the amplitude asked
        // for.
        const auto partialSum =
            (1.0f + shape_.secondPartial + shape_.thirdPartial + shape_.fourthPartial +
             shape_.fifthPartial + shape_.sixthPartial);
        const auto scale = 1.0f / ((1.0f + shape_.tremoloDepth) * (partialSum + shape_.noise));

        for (std::size_t index = 0; index < count; ++index)
        {
            const auto sway = vibratoRatio * static_cast<double>(read(vibratoPhase_)) +
                              driftRatio * static_cast<double>(read(driftPhase_));
            const auto swept = frequencyHz * std::pow(2.0, sway);
            const auto step = swept * turnsPerSample;

            const auto level =
                amplitude * (1.0f + shape_.tremoloDepth * read(tremoloPhase_)) * scale;

            // Where the loud band is right now, in partials.
            const auto centre =
                formantMid + formantSwing * static_cast<double>(read(formantPhase_));

            const auto voiced =
                emphasis(1.0, centre) * read(phase_) +
                shape_.secondPartial * emphasis(2.0, centre) * read(secondPhase_) +
                shape_.thirdPartial * emphasis(3.0, centre) * read(thirdPhase_) +
                shape_.fourthPartial * emphasis(4.0, centre) * read(fourthPhase_) +
                shape_.fifthPartial * emphasis(5.0, centre) * read(fifthPhase_) +
                shape_.sixthPartial * emphasis(6.0, centre) * read(sixthPhase_);

            destination[index] = level * (voiced + shape_.noise * noise());

            advance(phase_, step);
            advance(secondPhase_, step * 2.0);
            advance(thirdPhase_, step * 3.0);
            advance(fourthPhase_, step * 4.0);
            advance(fifthPhase_, step * 5.0);
            advance(sixthPhase_, step * 6.0);
            advance(formantPhase_, formantStep);
            advance(vibratoPhase_, vibratoStep);
            advance(driftPhase_, driftStep);
            advance(tremoloPhase_, tremoloStep);
        }
    }

  private:
    // How much this partial is lifted by the moving band. A bell around the
    // centre rather than a filter: the shape only has to be plausible and
    // smooth, and this costs two multiplies where a filter would cost state.
    [[nodiscard]] float emphasis(double partial, double centre) const noexcept
    {
        const auto distance = (partial - centre) / static_cast<double>(shape_.formantWidth);

        return 1.0f + shape_.formantDepth * static_cast<float>(1.0 / (1.0 + distance * distance));
    }

    [[nodiscard]] float read(double phase) const noexcept
    {
        return table_[static_cast<std::size_t>(phase) % tableSize];
    }

    static void advance(double& phase, double step) noexcept
    {
        phase += step;

        while (phase >= static_cast<double>(tableSize))
        {
            phase -= static_cast<double>(tableSize);
        }
    }

    // xorshift rather than a library generator: a handful of integer
    // operations, no allocation, and the same sequence every run.
    [[nodiscard]] float noise() noexcept
    {
        noiseState_ ^= noiseState_ << 13;
        noiseState_ ^= noiseState_ >> 17;
        noiseState_ ^= noiseState_ << 5;

        return static_cast<float>(static_cast<std::int32_t>(noiseState_)) /
               static_cast<float>(2147483648.0);
    }

    std::array<float, tableSize> table_{};
    Shape shape_{};
    double phase_ = 0.0;
    double secondPhase_ = 0.0;
    double thirdPhase_ = 0.0;
    double fourthPhase_ = 0.0;
    double fifthPhase_ = 0.0;
    double sixthPhase_ = 0.0;
    double formantPhase_ = 0.0;
    double vibratoPhase_ = 0.0;
    double driftPhase_ = 0.0;
    double tremoloPhase_ = 0.0;
    std::uint32_t noiseState_ = 0x9e3779b9u;
};
