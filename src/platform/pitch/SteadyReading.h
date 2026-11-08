#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

// Whether the note being heard is steady enough to take as a target: one note,
// within 30 cents of it, for 300 ms (no JUCE dependency).
// openspec/changes/archive/2026-11-08-target-note-picker/design.md, decision 3.
namespace pitch
{
class SteadyReading
{
  public:
    static constexpr double maximumCents = 30.0;
    static constexpr double minimumMilliseconds = 300.0;

    // One reading: the note the tuner shows and its cents from it, or no
    // note, at a time in milliseconds.
    void update(std::optional<int> note, double cents, double nowMilliseconds) noexcept
    {
        if (!note.has_value() || !std::isfinite(cents) || std::abs(cents) > maximumCents)
        {
            held.reset();
            return;
        }
        if (held != note)
        {
            held = note;
            since = nowMilliseconds;
        }
    }

    // The note held steadily for long enough, at `nowMilliseconds`.
    [[nodiscard]] std::optional<int> steadyNote(double nowMilliseconds) const noexcept
    {
        if (held.has_value() && nowMilliseconds - since >= minimumMilliseconds)
        {
            return held;
        }
        return std::nullopt;
    }

  private:
    std::optional<int> held;
    double since = 0.0;
};
} // namespace pitch
