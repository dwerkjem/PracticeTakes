#include "AccidentalStacking.h"

#include <algorithm>
#include <cstddef>

namespace engraving
{
namespace
{
// Overlaps are sums and differences of table values, compared against a
// clearance. A tolerance keeps a case that lands on the boundary from being
// decided by the last bit of a double.
constexpr double clearanceTolerance = 1e-9;

// Whether two signs can share a column. `upper` is the higher on the staff.
// Box against box, with room between: see `accidentalVerticalClearance`.
[[nodiscard]] bool clears(
    const AccidentalToStack& upper,
    const AccidentalToStack& lower,
    const LayoutOptions& options,
    double scale)
{
    const AccidentalMetrics upperMetrics = accidentalMetricsFor(upper.kind);
    const AccidentalMetrics lowerMetrics = accidentalMetricsFor(lower.kind);

    const double distance = spacesBelowTopLine(lower.position) - spacesBelowTopLine(upper.position);
    const double gap = distance - ((upperMetrics.below + lowerMetrics.above) * scale);

    return gap >= (options.accidentalVerticalClearance * scale) - clearanceTolerance;
}
} // namespace

AccidentalStack stackAccidentals(
    const std::vector<AccidentalToStack>& signs,
    const LayoutOptions& options,
    double scale)
{
    AccidentalStack stack;
    stack.x.assign(signs.size(), 0.0);
    stack.column.assign(signs.size(), -1);

    // Top to bottom. A smaller staff position is higher on the staff. Ties --
    // two signs on one position, a unison with two spellings -- keep input
    // order, so the result does not depend on the sort's mood.
    std::vector<std::size_t> topToBottom;

    for (std::size_t index = 0; index < signs.size(); ++index)
    {
        if (signs.at(index).kind != AccidentalKind::none)
        {
            topToBottom.push_back(index);
        }
    }

    if (topToBottom.empty())
    {
        return stack;
    }

    std::stable_sort(
        topToBottom.begin(), topToBottom.end(), [&signs](std::size_t lhs, std::size_t rhs)
        { return signs.at(lhs).position < signs.at(rhs).position; });

    // Outside in: top, bottom, second from top, second from bottom, ...
    std::vector<std::size_t> placingOrder;
    placingOrder.reserve(topToBottom.size());

    for (std::size_t front = 0, back = topToBottom.size(); front < back;)
    {
        placingOrder.push_back(topToBottom.at(front++));

        if (front < back)
        {
            placingOrder.push_back(topToBottom.at(--back));
        }
    }

    std::vector<std::vector<std::size_t>> columns;

    for (const std::size_t index : placingOrder)
    {
        const AccidentalToStack& sign = signs.at(index);

        const auto fits = [&](const std::vector<std::size_t>& members)
        {
            return std::all_of(
                members.begin(), members.end(),
                [&](std::size_t member)
                {
                    const AccidentalToStack& other = signs.at(member);

                    return sign.position >= other.position ? clears(other, sign, options, scale)
                                                           : clears(sign, other, options, scale);
                });
        };

        const auto column = std::find_if(columns.begin(), columns.end(), fits);

        if (column != columns.end())
        {
            column->push_back(index);
            stack.column.at(index) = static_cast<int>(column - columns.begin());
        }
        else
        {
            stack.column.at(index) = static_cast<int>(columns.size());
            columns.push_back({index});
        }
    }

    // Now that every column's members are known, place the columns right to
    // left. A column is as wide as its widest sign and its signs are
    // right-aligned in it, nearest the chord: the signs differ in width -- a
    // double flat is nearly twice a natural -- so it is their right edges that
    // line up, as they did when each was drawn against its own notehead.
    double right = -options.accidentalGap * scale;
    double left = right;

    for (const std::vector<std::size_t>& members : columns)
    {
        double columnWidth = 0.0;

        for (const std::size_t member : members)
        {
            const double width = accidentalMetricsFor(signs.at(member).kind).width * scale;
            columnWidth = std::max(columnWidth, width);
            stack.x.at(member) = right - width;
        }

        left = right - columnWidth;
        right = left - (options.accidentalColumnGap * scale);
    }

    stack.columnCount = static_cast<int>(columns.size());
    stack.width = -left;

    return stack;
}
} // namespace engraving
