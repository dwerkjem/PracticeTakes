#include "LevelMeter.h"

#include <algorithm>
#include <cmath>

LevelMeter::LevelMeter(std::size_t windowSize, std::size_t hop)
    : window(std::max<std::size_t>(windowSize, 1), 0.0f),
      hopSize(std::clamp<std::size_t>(hop, 1, std::max<std::size_t>(windowSize, 1)))
{
}

void LevelMeter::setSampleRate(double sampleRate)
{
    if (sampleRate > 0.0)
    {
        rate = sampleRate;
    }
    reset();
}

void LevelMeter::reset() noexcept
{
    std::fill(window.begin(), window.end(), 0.0f);
    next = 0;
    sinceMeasured = 0;
    holdLeftSeconds = 0.0;
    current = {};
}

void LevelMeter::push(const float* samples, std::size_t count)
{
    if (samples == nullptr)
    {
        return;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        window[next] = samples[index];
        next = (next + 1) % window.size();
        if (++sinceMeasured == hopSize)
        {
            sinceMeasured = 0;
            measure();
        }
    }
}

void LevelMeter::measure()
{
    // The window's RMS, as evaluation::levelDb takes it. Until the window has
    // filled, the samples not yet heard count as silence: a sound just
    // starting reads as quiet as it would at the very start of a take.
    double sum = 0.0;
    for (const float sample : window)
    {
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const double meanSquare = sum / static_cast<double>(window.size());
    const double measured =
        meanSquare > 0.0 ? std::max(silenceDb, 10.0 * std::log10(meanSquare)) : silenceDb;

    const double seconds = static_cast<double>(hopSize) / rate;

    // Up at once, down at the release rate.
    current.levelDb = std::max(measured, current.levelDb - (releaseDbPerSecond * seconds));
    current.levelDb = std::max(current.levelDb, silenceDb);

    if (current.levelDb >= current.peakDb)
    {
        current.peakDb = current.levelDb;
        holdLeftSeconds = peakHoldSeconds;
    }
    else if (holdLeftSeconds > 0.0)
    {
        holdLeftSeconds -= seconds;
    }
    else
    {
        current.peakDb =
            std::max(current.levelDb, current.peakDb - (peakReleaseDbPerSecond * seconds));
    }
}
