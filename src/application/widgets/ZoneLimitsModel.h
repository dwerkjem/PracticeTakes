#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Where three nested limits may go when one is dragged (no JUCE dependency):
// the rule behind ZoneLimitsSlider, which sets pitch's and timing's limits on
// one bar.
//
// The limits are the bands' edges, innermost first -- near perfect, in tune,
// out of tune; right on, on time, off the beat -- and each keeps the range and
// the order the separate controls kept: inside its own range, and at least
// `gap` inside the next looser one. A dragged limit that comes too close to a
// neighbour pushes it along rather than stopping.
namespace zone_limits
{
struct Limit
{
    double minimum = 0.0;
    double maximum = 0.0;
};

struct Model
{
    std::array<Limit, 3> limits;
    double gap = 1.0;
    double step = 1.0;

    // The bar runs from zero to the loosest limit's maximum.
    [[nodiscard]] double axisMaximum() const noexcept
    {
        return limits[2].maximum;
    }
};

// `values` with limit `index` moved as near `proposed` as it may go, on the
// model's step. A limit that comes within the gap of a neighbour pushes it
// along, and that one the next, so they stay a gap apart; the drag stops only
// where a pushed limit would leave its own range.
[[nodiscard]] inline std::array<double, 3>
moved(const Model& model, std::array<double, 3> values, std::size_t index, double proposed) noexcept
{
    if (index >= values.size() || !std::isfinite(proposed))
    {
        return values;
    }

    // As far as every limit it would push can still give way.
    double lowest = model.limits[index].minimum;
    double highest = model.limits[index].maximum;
    for (std::size_t other = 0; other < values.size(); ++other)
    {
        const double apart = model.gap * (static_cast<double>(index) - static_cast<double>(other));
        if (other < index)
        {
            lowest = std::max(lowest, model.limits[other].minimum + apart);
        }
        else if (other > index)
        {
            highest = std::min(highest, model.limits[other].maximum + apart);
        }
    }
    if (lowest > highest)
    {
        return values;
    }

    double value = proposed;
    if (model.step > 0.0)
    {
        value = std::round(value / model.step) * model.step;
    }
    values[index] = std::clamp(value, lowest, highest);

    // Then the push, outwards from the one dragged.
    for (std::size_t other = index + 1; other < values.size(); ++other)
    {
        values[other] = std::max(values[other], values[other - 1] + model.gap);
    }
    for (std::size_t other = index; other-- > 0;)
    {
        values[other] = std::min(values[other], values[other + 1] - model.gap);
    }
    return values;
}

// The limit a press at `value` takes hold of: the nearest, and of two at the
// same place the one the press is on the outside of, so a pair that has met
// can still be pulled apart.
[[nodiscard]] inline std::size_t nearest(const std::array<double, 3>& values, double value) noexcept
{
    std::size_t best = 0;
    for (std::size_t index = 1; index < values.size(); ++index)
    {
        const double distance = std::abs(values[index] - value);
        const double bestDistance = std::abs(values[best] - value);
        if (distance < bestDistance || (distance == bestDistance && value > values[index]))
        {
            best = index;
        }
    }
    return best;
}
} // namespace zone_limits
