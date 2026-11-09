#pragma once

#include <cstddef>
#include <vector>

// How loud the input is, measured the way a take's attacks are (no JUCE
// dependency).
//
// The level is the RMS of the last `windowSize` samples, in dB below full
// scale -- evaluation::levelDb over the same window the onset detector checks
// against "Ignore sound below" (#349). Measured the same way, so a sound the
// meter shows over the threshold line is a sound a take would hear, and one
// under it is one a take ignores. A sample peak meter would read several dB
// louder than that for the same voice, and the line would lie (#381).
//
// What is shown moves as a meter's does: it rises at once and falls at
// `releaseDbPerSecond`, so a short attack between two repaints is still seen.
// The peak is the loudest the level has been lately: held for `peakHoldSeconds`
// and then falling at `peakReleaseDbPerSecond`.
//
// Message thread only. A tool pushes what it drained from its FIFO and reads
// the result on the same timer.
class LevelMeter
{
  public:
    // What `reading` reports for silence, and the least either value can be.
    // Finite, so the arithmetic of falling never meets minus infinity.
    static constexpr double silenceDb = -120.0;

    static constexpr double defaultReleaseDbPerSecond = 30.0;
    static constexpr double defaultPeakHoldSeconds = 1.5;
    static constexpr double defaultPeakReleaseDbPerSecond = 15.0;

    struct Reading
    {
        // The level being shown, in dB below full scale.
        double levelDb = silenceDb;

        // The loudest the level has been lately.
        double peakDb = silenceDb;

        // Whether the level reaches `thresholdDb`: whether a take would count
        // the sound as playing or singing rather than ignore it.
        [[nodiscard]] bool reaches(double thresholdDb) const noexcept
        {
            return levelDb >= thresholdDb;
        }
    };

    // The window is measured every `hop` samples. Both at least one; a hop
    // larger than the window is the window.
    explicit LevelMeter(std::size_t windowSize = 1024, std::size_t hop = 256);

    // The rate the samples arrive at, which the fall rates and the hold are
    // per second of. Resets the meter: what was measured at another rate is
    // not this signal.
    void setSampleRate(double sampleRate);

    // Back to silence, as if nothing had been heard.
    void reset() noexcept;

    void push(const float* samples, std::size_t count);

    [[nodiscard]] Reading reading() const noexcept
    {
        return current;
    }

    [[nodiscard]] std::size_t windowSize() const noexcept
    {
        return window.size();
    }

    double releaseDbPerSecond = defaultReleaseDbPerSecond;
    double peakHoldSeconds = defaultPeakHoldSeconds;
    double peakReleaseDbPerSecond = defaultPeakReleaseDbPerSecond;

  private:
    // One measurement of the full window, every hop.
    void measure();

    std::vector<float> window;
    std::size_t hopSize;
    std::size_t next = 0;
    std::size_t sinceMeasured = 0;
    double rate = 44100.0;
    double holdLeftSeconds = 0.0;
    Reading current;
};
