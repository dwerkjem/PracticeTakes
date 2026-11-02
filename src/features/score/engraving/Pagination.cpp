#include "Pagination.h"

namespace engraving
{
std::vector<Page> paginate(const std::vector<System>& systems, double pageHeight)
{
    std::vector<Page> pages;

    // A little slack, so a system that fits exactly is not pushed to the next
    // page by rounding in the bands' arithmetic.
    constexpr double tolerance = 1.0e-9;

    for (std::size_t index = 0; index < systems.size(); ++index)
    {
        const System& system = systems.at(index);
        const double height = system.bandBottom - system.bandTop;

        if (!pages.empty() && !pages.back().overflows)
        {
            Page& current = pages.back();

            if (system.bandBottom - current.top <= pageHeight + tolerance)
            {
                current.lastSystem = index;

                continue;
            }
        }

        pages.push_back(Page{index, index, system.bandTop, height > pageHeight + tolerance});
    }

    return pages;
}
} // namespace engraving
