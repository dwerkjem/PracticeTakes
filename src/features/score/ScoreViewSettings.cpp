#include "ScoreViewSettings.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace score_view
{
std::size_t nearestSizeStep(double pixelsPerSpace)
{
    if (!std::isfinite(pixelsPerSpace))
    {
        return defaultSizeStep;
    }

    std::size_t nearest = defaultSizeStep;
    double nearestDistance = std::numeric_limits<double>::max();

    for (std::size_t step = 0; step < sizeLadder.size(); ++step)
    {
        const double distance = std::abs(sizeLadder.at(step) - pixelsPerSpace);

        if (distance < nearestDistance)
        {
            nearest = step;
            nearestDistance = distance;
        }
    }

    return nearest;
}

std::string_view nameOf(ViewMode view)
{
    switch (view)
    {
    case ViewMode::vertical:
        return "vertical";

    case ViewMode::horizontal:
        return "horizontal";

    case ViewMode::page:
        break;
    }

    return "page";
}

std::optional<ViewMode> viewNamed(std::string_view name)
{
    for (const ViewMode view : {ViewMode::page, ViewMode::vertical, ViewMode::horizontal})
    {
        if (nameOf(view) == name)
        {
            return view;
        }
    }

    return std::nullopt;
}

ToolSettingsPayload encode(const Settings& settings)
{
    const std::size_t step =
        settings.sizeStep < sizeLadder.size() ? settings.sizeStep : defaultSizeStep;

    // Shortest round-trip form, so 7.0 is "7" and 8.2 is "8.2".
    std::array<char, 32> size{};
    const auto written = std::to_chars(size.data(), size.data() + size.size(), sizeLadder.at(step));

    std::string data = "view=";
    data += nameOf(settings.view);
    data += ";size=";
    data.append(size.data(), written.ptr);

    return {settingsVersion, data};
}

Settings decode(const ToolSettingsPayload& payload)
{
    Settings settings;

    if (payload.version != settingsVersion)
    {
        return settings;
    }

    const std::string_view data = payload.data;
    std::size_t start = 0;

    while (start <= data.size())
    {
        const std::size_t end = std::min(data.find(';', start), data.size());
        const std::string_view field = data.substr(start, end - start);
        const std::size_t equals = field.find('=');

        if (equals != std::string_view::npos)
        {
            const std::string_view key = field.substr(0, equals);
            const std::string_view value = field.substr(equals + 1);

            if (key == "view")
            {
                if (const std::optional<ViewMode> view = viewNamed(value); view.has_value())
                {
                    settings.view = *view;
                }
            }
            else if (key == "size")
            {
                double size = 0.0;
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), size);

                if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                    size > 0.0)
                {
                    settings.sizeStep = nearestSizeStep(size);
                }
            }
        }

        start = end + 1;
    }

    return settings;
}
} // namespace score_view
