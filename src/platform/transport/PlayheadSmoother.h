#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

// Where to draw the practice clock's playhead between its notifications (no
// JUCE dependency).
//
// The clock reports its position at 60 Hz, but the position itself advances in
// whole audio blocks, so one report moves a block further than the last and the
// next two blocks: a cursor drawn straight from the reports moves in uneven
// steps, and stands still for a frame now and then. This draws it where the
// clock *will* be at the moment of drawing, from the rate it has been moving
// at, and pulls that prediction gently towards each report rather than jumping
// to it. A report far from the prediction -- a seek, a loop's wrap, a change of
// tempo the rate has not caught up with -- is taken as it is.
namespace transport
{
class PlayheadSmoother
{
  public:
    // A report from the clock: its tick, the time it arrived in seconds on
    // any steady clock, and whether the clock is playing. A stopped or paused
    // clock is drawn exactly where it is.
    //
    // `knownRate`, when given, is how fast the value moves in units a second
    // -- the score's seconds move at the playback scale, whatever the tempo --
    // and is used as it is rather than learned from the reports.
    void observe(double tick, double now, bool playing, std::optional<double> knownRate = {})
    {
        if (!playing)
        {
            reset(tick, now);
            return;
        }

        if (!running)
        {
            reset(tick, now);
            running = true;
            rate = knownRate.value_or(0.0);
            return;
        }

        const double elapsed = now - lastTime;
        const double predicted = at(now);
        const double moved = tick - lastTick;
        const double error = tick - predicted;

        // Not a step along the same line -- backwards, or further from the
        // prediction than jitter could put it: a jump the drawing makes too.
        // Tested against the rate already learned, before this report can
        // inflate it.
        if (moved < 0.0 || (rate > 0.0 && std::abs(error) > rate * snapSeconds))
        {
            const double kept = rate;
            reset(tick, now);
            running = true;
            rate = kept;
            return;
        }

        // The rate, settled over many reports: one report's own rate is as
        // uneven as the blocks it counts. The first is taken as it is, and
        // drawn from exactly.
        const bool learning = rate <= 0.0 && !knownRate.has_value();
        if (knownRate.has_value())
        {
            rate = *knownRate;
        }
        else if (elapsed > 0.0)
        {
            const double instant = moved / elapsed;
            rate = learning ? instant : rate + (rateSmoothing * (instant - rate));
        }

        baseTick = learning ? tick : predicted + (correction * error);
        baseTime = now;
        lastTick = tick;
        lastTime = now;
    }

    // Forget the motion: the next report is drawn exactly, then motion is
    // learned again. For a seek or a wrap, which are jumps by definition.
    void restart(double tick, double now)
    {
        const bool wasRunning = running;
        const double kept = rate;
        reset(tick, now);
        running = wasRunning;
        rate = kept;
    }

    // The tick to draw at `now`. Never more than a fifth of a second ahead of
    // the last report, so a clock that stops reporting does not run away.
    [[nodiscard]] double at(double now) const
    {
        if (!running || rate <= 0.0)
        {
            return baseTick;
        }
        const double ahead = std::clamp(now - baseTime, 0.0, maximumLeadSeconds);
        return baseTick + (rate * ahead);
    }

    [[nodiscard]] bool isRunning() const noexcept
    {
        return running;
    }

  private:
    void reset(double tick, double now)
    {
        running = false;
        baseTick = tick;
        baseTime = now;
        lastTick = tick;
        lastTime = now;
        rate = 0.0;
    }

    // How much of each report's rate moves the settled one, and how much of
    // each report's distance from the prediction is closed at once.
    static constexpr double rateSmoothing = 0.1;
    static constexpr double correction = 0.2;

    // A report further than this from the prediction, in seconds of music at
    // the current rate, is a jump rather than jitter.
    static constexpr double snapSeconds = 0.25;
    static constexpr double maximumLeadSeconds = 0.2;

    bool running = false;
    double baseTick = 0.0;
    double baseTime = 0.0;
    double lastTick = 0.0;
    double lastTime = 0.0;

    // Ticks per second.
    double rate = 0.0;
};
} // namespace transport
