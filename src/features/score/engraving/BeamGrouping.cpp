#include "BeamGrouping.h"

namespace engraving
{
namespace
{
// Which group a tick falls in. Integer division, so a note that starts inside a
// group stays with it.
[[nodiscard]] score::Tick groupIndexOf(score::Tick onset, score::Tick groupTicks)
{
    return onset / groupTicks;
}
} // namespace

bool isCompoundMetre(const score::TimeSignature& time)
{
    if (time.beats <= 0 || time.beatType <= 0)
    {
        return false;
    }

    // The beat has to be an eighth or shorter for three of them to make one
    // dotted beat worth grouping: 3/4 is three beats, not one compound beat.
    return time.beatType >= 8 && time.beats % 3 == 0;
}

score::Tick beamGroupTicks(const score::TimeSignature& time)
{
    if (time.beats <= 0 || time.beatType <= 0)
    {
        return 0;
    }

    const score::Tick beatUnit = score::ticksPerWholeNote / time.beatType;

    if (beatUnit <= 0)
    {
        return 0;
    }

    return isCompoundMetre(time) ? beatUnit * 3 : beatUnit;
}

std::vector<std::vector<std::size_t>>
beamGroupsFor(const std::vector<BeamCandidate>& candidates, const score::TimeSignature& time)
{
    std::vector<std::vector<std::size_t>> groups;

    const score::Tick groupTicks = beamGroupTicks(time);

    if (groupTicks <= 0)
    {
        return groups;
    }

    std::vector<std::size_t> run;
    score::Tick runGroup = 0;
    score::Tick runEnd = 0;

    // Close the run that is open, keeping it only if two notes actually share
    // a beam. A run of one is a flagged note and is not a group.
    const auto flush = [&groups, &run]()
    {
        if (run.size() >= 2)
        {
            groups.push_back(run);
        }

        run.clear();
    };

    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        const BeamCandidate& candidate = candidates.at(index);

        if (candidate.beamLevels <= 0)
        {
            flush();

            continue;
        }

        const score::Tick group = groupIndexOf(candidate.onset, groupTicks);

        const bool continues = !run.empty() && group == runGroup && candidate.onset == runEnd;

        if (!continues)
        {
            flush();

            runGroup = group;
        }

        run.push_back(index);
        runEnd = candidate.onset + candidate.duration;
    }

    flush();

    return groups;
}
} // namespace engraving
