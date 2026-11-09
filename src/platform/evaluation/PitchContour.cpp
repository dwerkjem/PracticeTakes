#include "PitchContour.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "Fft.h"

namespace evaluation
{
namespace
{
// The inverse transform through the forward one: conj(fft(conj(x))) / n.
void inverseFft(std::vector<std::complex<float>>& data)
{
    for (std::complex<float>& value : data)
    {
        value = std::conj(value);
    }
    fft(data);
    const float scale = 1.0f / static_cast<float>(data.size());
    for (std::complex<float>& value : data)
    {
        value = std::conj(value) * scale;
    }
}

// The working storage for one window, kept between windows.
struct Scratch
{
    explicit Scratch(std::size_t window)
        : integration(window / 2), packed(window), cumulative(window + 1),
          difference(window / 2 + 1), normalised(window / 2 + 1)
    {
    }

    std::size_t integration;
    std::vector<std::complex<float>> packed;
    std::vector<double> cumulative;
    std::vector<double> difference;
    std::vector<double> normalised;
};

// YIN on one window of `window` samples: the integration runs over the first
// half, and the lag up to the second half (de Cheveigné and Kawahara, 2002).
[[nodiscard]] ContourFrame analyse(
    const float* samples,
    std::size_t window,
    double sampleRate,
    const ContourSettings& settings,
    Scratch& scratch)
{
    ContourFrame result;
    const std::size_t half = scratch.integration;

    scratch.cumulative[0] = 0.0;
    for (std::size_t index = 0; index < window; ++index)
    {
        const auto sample = static_cast<double>(samples[index]);
        scratch.cumulative[index + 1] = scratch.cumulative[index] + (sample * sample);
    }
    result.rms = std::sqrt(scratch.cumulative[window] / static_cast<double>(window));
    if (result.rms < settings.minimumRms)
    {
        return result;
    }

    const auto shortest =
        static_cast<std::size_t>(std::max(2.0, std::floor(sampleRate / settings.maximumHz)));
    const std::size_t longest =
        std::min(half, static_cast<std::size_t>(std::ceil(sampleRate / settings.minimumHz)));
    if (shortest + 2 >= longest)
    {
        return result;
    }

    // The cross-correlation of the integration half with the whole window,
    // both real, packed into one complex transform: the first in the real
    // part, the second in the imaginary. The transform is the window's length:
    // for lags up to half of it, no product wraps around.
    std::vector<std::complex<float>>& packed = scratch.packed;
    for (std::size_t index = 0; index < packed.size(); ++index)
    {
        const float whole = samples[index];
        const float first = index < half ? samples[index] : 0.0f;
        packed[index] = {first, whole};
    }
    fft(packed);
    const std::size_t size = packed.size();
    // Unpacked: A = (Z[k] + conj(Z[n-k])) / 2, B = (Z[k] - conj(Z[n-k])) / 2i;
    // the correlation's spectrum is conj(A) * B. Computed for k and n-k
    // together, so it is written in place.
    for (std::size_t k = 0; k <= size / 2; ++k)
    {
        const std::size_t mirror = (size - k) % size;
        const std::complex<float> z = packed[k];
        const std::complex<float> zm = std::conj(packed[mirror]);
        const std::complex<float> a = (z + zm) * 0.5f;
        const std::complex<float> b = (z - zm) * std::complex<float>(0.0f, -0.5f);
        const std::complex<float> product = std::conj(a) * b;
        packed[k] = product;
        if (mirror != k)
        {
            // The correlation is real, so its spectrum is conjugate symmetric.
            packed[mirror] = std::conj(product);
        }
    }
    inverseFft(packed);

    // d(tau) = sum x[j]^2 + sum x[j+tau]^2 - 2 r(tau), over the integration half.
    const double firstEnergy = scratch.cumulative[half];
    scratch.difference[0] = 0.0;
    for (std::size_t lag = 1; lag <= longest; ++lag)
    {
        const double shifted = scratch.cumulative[lag + half] - scratch.cumulative[lag];
        const double correlation = static_cast<double>(packed[lag].real());
        scratch.difference[lag] = std::max(0.0, firstEnergy + shifted - (2.0 * correlation));
    }

    // Cumulative mean normalised.
    scratch.normalised[0] = 1.0;
    double running = 0.0;
    for (std::size_t lag = 1; lag <= longest; ++lag)
    {
        running += scratch.difference[lag];
        scratch.normalised[lag] =
            running > 0.0 ? scratch.difference[lag] * static_cast<double>(lag) / running : 1.0;
    }

    // The first dip below the threshold, followed to its bottom; otherwise the
    // lowest point, which a low confidence then reports as unsure.
    std::size_t chosen = 0;
    for (std::size_t lag = shortest; lag < longest; ++lag)
    {
        if (scratch.normalised[lag] < settings.threshold)
        {
            while (lag + 1 < longest && scratch.normalised[lag + 1] < scratch.normalised[lag])
            {
                ++lag;
            }
            chosen = lag;
            break;
        }
    }
    if (chosen == 0)
    {
        chosen = shortest;
        for (std::size_t lag = shortest; lag < longest; ++lag)
        {
            if (scratch.normalised[lag] < scratch.normalised[chosen])
            {
                chosen = lag;
            }
        }
    }
    if (chosen <= shortest || chosen + 1 >= longest)
    {
        // At the edge of the range: no period inside it.
        return result;
    }

    const double left = scratch.normalised[chosen - 1];
    const double centre = scratch.normalised[chosen];
    const double right = scratch.normalised[chosen + 1];
    const double curvature = left - (2.0 * centre) + right;
    const double offset =
        curvature > 0.0 ? std::clamp(0.5 * (left - right) / curvature, -0.5, 0.5) : 0.0;
    const double period = static_cast<double>(chosen) + offset;
    const double bottom = centre - (0.25 * (left - right) * offset);

    result.hertz = sampleRate / period;
    result.confidence = std::clamp(1.0 - bottom, 0.0, 1.0);
    return result;
}
} // namespace

std::size_t contourWindowSize(double sampleRate, const ContourSettings& settings)
{
    const double wanted = std::max(64.0, settings.windowSeconds * sampleRate);
    const double exponent = std::round(std::log2(wanted));
    return static_cast<std::size_t>(std::exp2(exponent));
}

PitchContour pitchContourOf(
    const float* audio,
    std::size_t count,
    double sampleRate,
    std::int64_t startFrame,
    const ContourSettings& settings)
{
    PitchContour contour;
    if (audio == nullptr || sampleRate <= 0.0)
    {
        return contour;
    }
    const std::size_t window = contourWindowSize(sampleRate, settings);
    const std::size_t hop =
        std::max<std::size_t>(1, window / std::max<std::size_t>(1, settings.hopsPerWindow));
    contour.hop = static_cast<std::int64_t>(hop);
    if (count < window)
    {
        return contour;
    }

    Scratch scratch(window);
    contour.frames.reserve(((count - window) / hop) + 1);
    for (std::size_t start = 0; start + window <= count; start += hop)
    {
        ContourFrame frame = analyse(audio + start, window, sampleRate, settings, scratch);
        frame.frame = startFrame + static_cast<std::int64_t>(start + (window / 2));
        contour.frames.push_back(frame);
    }
    return contour;
}
} // namespace evaluation
