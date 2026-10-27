#include "BeamGeometry.h"

#include <algorithm>
#include <cmath>

namespace engraving
{
namespace
{
// How far a beam must stay clear of the far notehead of a chord, in staff
// spaces. A beam resting on a notehead reads as a thick notehead.
constexpr double farNoteheadClearance = 1.0;

// A beam group's own straight line, in spaces below the top line.
struct BeamLine
{
    double startX = 0.0;
    double endX = 0.0;
    double startY = 0.0;
    double endY = 0.0;

    [[nodiscard]] double at(double x) const
    {
        if (endX <= startX)
        {
            return startY;
        }

        return startY + ((endY - startY) * ((x - startX) / (endX - startX)));
    }

    void shiftBy(double delta)
    {
        startY += delta;
        endY += delta;
    }
};

// Maximal runs of consecutive stems carrying at least `level` beams.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>>
runsAtLevel(const std::vector<BeamStem>& stems, int level)
{
    std::vector<std::pair<std::size_t, std::size_t>> runs;

    std::size_t index = 0;

    while (index < stems.size())
    {
        if (stems.at(index).beamLevels < level)
        {
            ++index;

            continue;
        }

        const std::size_t first = index;

        while (index < stems.size() && stems.at(index).beamLevels >= level)
        {
            ++index;
        }

        runs.emplace_back(first, index - 1);
    }

    return runs;
}
} // namespace

BeamGroupGeometry
layOutBeamGroup(const std::vector<BeamStem>& stems, bool up, const LayoutOptions& options)
{
    BeamGroupGeometry geometry;

    if (stems.size() < 2)
    {
        return geometry;
    }

    // One sign carries the whole of "up". y grows downwards, so an upward stem
    // ends at a smaller y and its extra beams stack towards smaller y still.
    const double sign = up ? -1.0 : 1.0;

    const double defaultLength = static_cast<double>(options.stemLength) * 0.5;

    const auto naturalTipOf = [&](const BeamStem& stem)
    { return spacesBelowTopLine(stem.attachPosition) + (sign * defaultLength); };

    BeamLine line{
        stems.front().x, stems.back().x, naturalTipOf(stems.front()), naturalTipOf(stems.back())};

    // Clamp the rise. The beam follows the shape of its notes; a leap of two
    // octaves inside a group would otherwise draw a diagonal.
    const double rise = line.endY - line.startY;
    const double clamped = std::clamp(rise, -options.maximumBeamRise, options.maximumBeamRise);

    line.endY = line.startY + clamped;

    // Push the line away from the staff until every stem clears. `sign *
    // shortfall` turns both directions into one comparison: a positive
    // shortfall is a stem that has come out too short whichever way it points.
    double worstShortfall = 0.0;

    for (const BeamStem& stem : stems)
    {
        // Not `near`/`far`: both are macros in the Windows headers, and this
        // target builds there.
        const double nearY = spacesBelowTopLine(stem.attachPosition);
        const double farY = spacesBelowTopLine(stem.farPosition);

        const double requiredFromNear = nearY + (sign * options.minimumStemLength);
        const double requiredFromFar = farY + (sign * farNoteheadClearance);

        // The further of the two, in the direction the stem points.
        const double required = up ? std::min(requiredFromNear, requiredFromFar)
                                   : std::max(requiredFromNear, requiredFromFar);

        const double shortfall = sign * (required - line.at(stem.x));

        worstShortfall = std::max(worstShortfall, shortfall);
    }

    if (worstShortfall > 0.0)
    {
        line.shiftBy(sign * worstShortfall);
    }

    // Every stem reaches the outermost beam that meets it, which is its own
    // level: each level up to `beamLevels` is either a bar or a stub at this
    // stem, by construction below.
    const double beamPitch = options.beamThickness + options.beamSeparation;

    // Half a stem: how far a beam reaches past the centre line of a stem at
    // its end, to that stem's outer edge.
    const double overhang = options.stemThickness * 0.5;

    geometry.stemTips.reserve(stems.size());

    for (const BeamStem& stem : stems)
    {
        const double outermost =
            line.at(stem.x) + (sign * static_cast<double>(stem.beamLevels - 1) * beamPitch);

        geometry.stemTips.push_back(outermost + (sign * options.beamThickness));
    }

    const int maximumLevel =
        std::max_element(
            stems.begin(), stems.end(), [](const BeamStem& lhs, const BeamStem& rhs)
            { return lhs.beamLevels < rhs.beamLevels; })
            ->beamLevels;

    for (int level = 1; level <= maximumLevel; ++level)
    {
        const double offset = sign * static_cast<double>(level - 1) * beamPitch;

        for (const std::pair<std::size_t, std::size_t>& run : runsAtLevel(stems, level))
        {
            const BeamStem& first = stems.at(run.first);
            const BeamStem& last = stems.at(run.second);

            if (run.first != run.second)
            {
                // Out to the stems' outer edges. Through `line.at` rather than
                // by extending x alone: a sloped bar extended sideways without
                // following its slope slides off the line it was laid out on,
                // and every stem pokes past it by the slope times the overhang.
                const double startX = first.x - overhang;
                const double endX = last.x + overhang;

                geometry.beams.push_back(BeamGlyph{
                    startX, endX, line.at(startX) + offset, line.at(endX) + offset,
                    options.beamThickness, level, false, up});

                continue;
            }

            // A level with no neighbour is a stub. It points back towards the
            // note before it -- towards the beat -- and only forwards when
            // there is nothing behind it, which is the conventional rule and
            // the one a reader uses to tell which note the stub belongs to.
            const bool pointsBack = run.first > 0;

            const std::size_t neighbourIndex = pointsBack ? run.first - 1 : run.first + 1;
            const double neighbourX = stems.at(neighbourIndex).x;

            // Never more than halfway to the neighbour, or two stubs facing
            // each other would touch and read as a bar.
            const double reach =
                std::min(options.partialBeamLength, std::abs(neighbourX - first.x) * 0.5);

            // The stub's own end reaches its stem's outer edge, like a full
            // beam's; its free end stops at `reach`.
            const double startX = pointsBack ? first.x - reach : first.x - overhang;
            const double endX = pointsBack ? first.x + overhang : first.x + reach;

            geometry.beams.push_back(BeamGlyph{
                startX, endX, line.at(startX) + offset, line.at(endX) + offset,
                options.beamThickness, level, true, up});
        }
    }

    return geometry;
}
} // namespace engraving
