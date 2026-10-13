#include "Beams.h"

#include <algorithm>
#include <cmath>

namespace score::engraving
{
Tick beamGroupTicks(const TimeSignature& time) noexcept
{
    const int beatType = time.beatType > 0 ? time.beatType : 4;

    const Tick division = ticksPerWholeNote / beatType;

    if (division <= 0)
    {
        return ticksPerQuarterNote;
    }

    return isCompound(time) ? division * 3 : division;
}

double BeamLine::stepAt(double x) const noexcept
{
    const double span = endX - startX;

    if (std::abs(span) < 1e-9)
    {
        return startStep;
    }

    return startStep + ((x - startX) * (endStep - startStep) / span);
}

BeamLine clampedBeamLine(double startX, double startStep, double endX, double endStep) noexcept
{
    BeamLine line{startX, startStep, endX, endStep};

    const double rise = endStep - startStep;

    if (std::abs(rise) <= maximumBeamRiseSteps)
    {
        return line;
    }

    // Clamped about the midpoint rather than about the first note, so the beam
    // stays centred on the group it belongs to.
    const double middle = (startStep + endStep) * 0.5;
    const double half = (rise < 0.0 ? -maximumBeamRiseSteps : maximumBeamRiseSteps) * 0.5;

    line.startStep = middle - half;
    line.endStep = middle + half;

    return line;
}

BeamLine shiftedToClear(
    BeamLine line,
    const std::vector<std::pair<double, double>>& required,
    bool up) noexcept
{
    double shift = 0.0;

    for (const std::pair<double, double>& stem : required)
    {
        const double atX = line.stepAt(stem.first);

        // A beam may lengthen a stem and must never shorten one.
        const double deficit = up ? stem.second - atX : atX - stem.second;

        shift = std::max(shift, deficit);
    }

    if (shift <= 0.0)
    {
        return line;
    }

    const double offset = up ? shift : -shift;

    line.startStep += offset;
    line.endStep += offset;

    return line;
}
} // namespace score::engraving
