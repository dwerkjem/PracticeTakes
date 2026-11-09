#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <vector>

// Everything the Note View draws that can be tested without a display: which
// keys it shows, where a key or a pitch sits across them, the trail of recent
// pitch, and how a lit key is shaded (no JUCE dependency).
// openspec/changes/note-view/design.md, decision 2.
namespace notes
{
inline constexpr double trailSeconds = 3.0;

// Within the tuner's in-tune band a lit key is fully "on"; by half a semitone
// it is fully "off".
inline constexpr double inTuneCents = 5.0;
inline constexpr double furthestCents = 50.0;

[[nodiscard]] constexpr bool isBlackKey(int midi) noexcept
{
    const int pitchClass = ((midi % 12) + 12) % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 ||
           pitchClass == 10;
}

// A key's centre, in white-key widths from C-1 (MIDI 0). A black key sits on
// the boundary between its two white neighbours.
[[nodiscard]] constexpr double keyCentre(int midi) noexcept
{
    constexpr int whiteBelow[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    const int octave = midi >= 0 ? midi / 12 : ((midi + 1) / 12) - 1;
    const int pitchClass = midi - (octave * 12);
    const double white = (octave * 7) + whiteBelow[pitchClass];
    return isBlackKey(midi) ? white + 1.0 : white + 0.5;
}

// A fractional pitch across the keys, between the centres of the two keys it
// lies between, so a slide is a line.
[[nodiscard]] inline double pitchX(double midi) noexcept
{
    const double lower = std::floor(midi);
    const auto key = static_cast<int>(lower);
    const double along = midi - lower;
    return keyCentre(key) + ((keyCentre(key + 1) - keyCentre(key)) * along);
}

// The keys shown: from a C, at least two octaves.
struct KeyWindow
{
    int lowest = 60;
    int highest = 84;

    [[nodiscard]] bool contains(int midi) const noexcept
    {
        return midi >= lowest && midi <= highest;
    }

    bool operator==(const KeyWindow&) const = default;
};

// The window that shows every key in `mustShow`. The current one is kept while
// it still does, so the keys hold still while the note moves inside them.
[[nodiscard]] inline KeyWindow windowFor(const KeyWindow& current, const std::vector<int>& mustShow)
{
    if (mustShow.empty())
    {
        return current;
    }
    const auto [lowestNeeded, highestNeeded] =
        std::minmax_element(mustShow.begin(), mustShow.end());
    if (current.contains(*lowestNeeded) && current.contains(*highestNeeded))
    {
        return current;
    }
    KeyWindow window;
    const auto floorToC = [](int midi) { return midi >= 0 ? (midi / 12) * 12 : 0; };
    window.lowest = std::clamp(floorToC(*lowestNeeded), 0, 103);
    window.highest = std::max(window.lowest + 24, floorToC(*highestNeeded) + 12);
    window.highest = std::min(window.highest, 127);
    return window;
}

// 0 for a key sung in tune, rising to 1 by half a semitone off.
[[nodiscard]] inline double offTune(double cents) noexcept
{
    const double off = std::abs(cents);
    if (off <= inTuneCents)
    {
        return 0.0;
    }
    return std::min(1.0, (off - inTuneCents) / (furthestCents - inTuneCents));
}

struct TrailPoint
{
    double seconds = 0.0;

    // No pitch: the trail breaks here.
    std::optional<double> midi;
};

// The last few seconds of pitch, oldest first.
class Trail
{
  public:
    void add(double seconds, std::optional<double> midi)
    {
        points.push_back({seconds, midi});
        while (!points.empty() && points.front().seconds < seconds - trailSeconds)
        {
            points.pop_front();
        }
    }

    void clear()
    {
        points.clear();
    }

    [[nodiscard]] const std::deque<TrailPoint>& recent() const noexcept
    {
        return points;
    }

  private:
    std::deque<TrailPoint> points;
};
} // namespace notes
