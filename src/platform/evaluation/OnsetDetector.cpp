#include "OnsetDetector.h"

#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace evaluation
{
namespace
{
[[nodiscard]] std::vector<float> hannWindow(std::size_t size)
{
    constexpr double twoPi = 6.283185307179586;
    std::vector<float> window(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        window[index] = static_cast<float>(
            0.5 - (0.5 * std::cos(twoPi * static_cast<double>(index) / static_cast<double>(size))));
    }
    return window;
}

// The spectral flux of every hop: how much the log magnitude spectrum rose
// since the hop before, summed over the bins that rose.
[[nodiscard]] std::vector<double>
spectralFlux(const float* audio, std::size_t count, const OnsetSettings& settings)
{
    const std::size_t size = settings.windowSize;
    const std::size_t hops = count / settings.hop + 1;
    const std::vector<float> window = hannWindow(size);
    std::vector<std::complex<float>> buffer(size);
    std::vector<float> previous(size / 2 + 1, 0.0f);
    std::vector<float> magnitude(size / 2 + 1, 0.0f);
    std::vector<double> flux(hops, 0.0);

    // Under each bin, a floor: what sound at the minimum level puts in one bin
    // of a Hann window (its RMS times the root of the window's energy, 3N/8),
    // doubled to clear the noise's own spread. Quieter than that, a bin rising
    // is the room, not an attack: the log spectrum makes a near-empty bin's
    // small rise look large, which is how noise under a held note became an
    // extra note.
    const float floor = static_cast<float>(
        2.0 * std::pow(10.0, settings.minimumLevelDb / 20.0) *
        std::sqrt(3.0 * static_cast<double>(size) / 8.0));

    for (std::size_t hop = 0; hop < hops; ++hop)
    {
        // Centred on the hop's position, zero beyond the audio's ends.
        const auto centre = static_cast<std::int64_t>(hop * settings.hop);
        const std::int64_t first = centre - static_cast<std::int64_t>(size / 2);
        for (std::size_t index = 0; index < size; ++index)
        {
            const std::int64_t sample = first + static_cast<std::int64_t>(index);
            const float value = sample >= 0 && sample < static_cast<std::int64_t>(count)
                                    ? audio[static_cast<std::size_t>(sample)]
                                    : 0.0f;
            buffer[index] = std::complex<float>(value * window[index], 0.0f);
        }
        fft(buffer);

        double rise = 0.0;
        for (std::size_t bin = 0; bin < magnitude.size(); ++bin)
        {
            // Not std::abs, whose hypot guards against overflow this cannot have.
            const float re = buffer[bin].real();
            const float im = buffer[bin].imag();
            magnitude[bin] = std::log1p(10.0f * std::max(floor, std::sqrt((re * re) + (im * im))));
            rise += std::max(0.0f, magnitude[bin] - previous[bin]);
        }
        flux[hop] = hop == 0 ? 0.0 : rise;
        std::swap(previous, magnitude);
    }
    return flux;
}
} // namespace

double levelDb(const float* audio, std::size_t first, std::size_t count) noexcept
{
    if (audio == nullptr || count == 0)
    {
        return -std::numeric_limits<double>::infinity();
    }
    double sum = 0.0;
    for (std::size_t index = first; index < first + count; ++index)
    {
        const double sample = audio[index];
        sum += sample * sample;
    }
    const double meanSquare = sum / static_cast<double>(count);
    return meanSquare > 0.0 ? 10.0 * std::log10(meanSquare)
                            : -std::numeric_limits<double>::infinity();
}

std::vector<PerformedOnset> detectOnsets(
    const float* audio,
    std::size_t count,
    double sampleRate,
    std::int64_t startFrame,
    const OnsetSettings& settings)
{
    std::vector<PerformedOnset> onsets;
    if (audio == nullptr || count == 0 || sampleRate <= 0.0 || !isPowerOfTwo(settings.windowSize) ||
        settings.hop == 0)
    {
        return onsets;
    }

    const std::vector<double> flux = spectralFlux(audio, count, settings);
    const double loudest = *std::max_element(flux.begin(), flux.end());
    if (loudest <= 0.0)
    {
        return onsets;
    }

    const auto minimumGap = static_cast<std::int64_t>(settings.minimumGapSeconds * sampleRate);
    std::vector<double> neighbourhood;

    const std::size_t half = settings.windowSize / 2;
    for (std::size_t hop = 1; hop + 1 < flux.size(); ++hop)
    {
        const std::size_t centre = hop * settings.hop;
        if (centre < half || centre + half > count)
        {
            continue;
        }
        if (flux[hop] < flux[hop - 1] || flux[hop] <= flux[hop + 1])
        {
            continue;
        }

        const std::size_t from = hop > settings.medianRadius ? hop - settings.medianRadius : 0;
        const std::size_t to = std::min(flux.size(), hop + settings.medianRadius + 1);
        neighbourhood.assign(
            flux.begin() + static_cast<std::ptrdiff_t>(from),
            flux.begin() + static_cast<std::ptrdiff_t>(to));
        const auto middle =
            neighbourhood.begin() + static_cast<std::ptrdiff_t>(neighbourhood.size() / 2);
        std::nth_element(neighbourhood.begin(), middle, neighbourhood.end());
        const double threshold =
            (settings.medianScale * *middle) + (settings.peakFraction * loudest);
        if (flux[hop] <= threshold)
        {
            continue;
        }

        // Loud enough to be playing or singing: the window that follows the
        // peak, where the attack's sound is, reaches the floor.
        const std::size_t attack = std::min(
            centre + static_cast<std::size_t>(
                         settings.peakLeadFraction * static_cast<double>(settings.windowSize)),
            count - 1);
        const std::size_t span = std::min(settings.windowSize, count - attack);
        if (levelDb(audio, attack, span) < settings.minimumLevelDb)
        {
            continue;
        }

        // Between hops: the vertex of the parabola through the peak and its
        // neighbours.
        const double before = flux[hop - 1];
        const double after = flux[hop + 1];
        const double curvature = before - (2.0 * flux[hop]) + after;
        const double shift = curvature != 0.0 ? 0.5 * (before - after) / curvature : 0.0;
        const auto frame =
            startFrame +
            static_cast<std::int64_t>(std::llround(
                ((static_cast<double>(hop) + std::clamp(shift, -0.5, 0.5)) *
                 static_cast<double>(settings.hop)) +
                (settings.peakLeadFraction * static_cast<double>(settings.windowSize))));
        const double strength = std::clamp((flux[hop] - threshold) / loudest, 0.0, 1.0);

        if (!onsets.empty() && frame - onsets.back().frame < minimumGap)
        {
            // Too close to the last: keep the stronger of the two.
            if (strength > onsets.back().strength)
            {
                onsets.back() = PerformedOnset{frame, strength, {}};
            }
            continue;
        }
        onsets.push_back(PerformedOnset{frame, strength, {}});
    }

    // Strength relative to the strongest attack in the take, so the weakest
    // real attack is not mistaken for noise only because a loud one is near.
    double strongest = 0.0;
    for (const PerformedOnset& onset : onsets)
    {
        strongest = std::max(strongest, onset.strength);
    }
    if (strongest > 0.0)
    {
        for (PerformedOnset& onset : onsets)
        {
            onset.strength /= strongest;
        }
    }
    return onsets;
}
} // namespace evaluation
