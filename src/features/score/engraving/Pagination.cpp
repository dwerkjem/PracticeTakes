#include "Pagination.h"

#include <algorithm>

namespace engraving
{
namespace
{
// A little slack, so a system that fits exactly is not pushed to the next page
// by rounding in the bands' arithmetic.
constexpr double tolerance = 1.0e-9;
} // namespace

std::vector<Page> paginate(const std::vector<System>& systems, double pageHeight, double pageWidth)
{
    std::vector<Page> pages;

    for (std::size_t index = 0; index < systems.size(); ++index)
    {
        const System& system = systems.at(index);
        const double height = system.bandBottom - system.bandTop;
        const bool oversize =
            height > pageHeight + tolerance || system.width > pageWidth + tolerance;

        if (!oversize && !pages.empty() && !pages.back().oversize)
        {
            Page& current = pages.back();

            if (system.bandBottom - current.top <= pageHeight + tolerance)
            {
                current.lastSystem = index;

                continue;
            }
        }

        pages.push_back(Page{index, index, system.bandTop, oversize});
    }

    return pages;
}

double
fitScale(const Page& page, const std::vector<System>& systems, double pageHeight, double pageWidth)
{
    if (page.firstSystem >= systems.size() || page.lastSystem >= systems.size())
    {
        return 1.0;
    }

    double widest = 0.0;

    for (std::size_t index = page.firstSystem; index <= page.lastSystem; ++index)
    {
        widest = std::max(widest, systems.at(index).width);
    }

    const double tallest = systems.at(page.lastSystem).bandBottom - page.top;
    double scale = 1.0;

    if (tallest > pageHeight && tallest > 0.0)
    {
        scale = std::min(scale, pageHeight / tallest);
    }

    if (widest > pageWidth && widest > 0.0)
    {
        scale = std::min(scale, pageWidth / widest);
    }

    return scale;
}
} // namespace engraving
