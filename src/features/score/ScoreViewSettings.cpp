#include "ScoreViewSettings.h"

#include <cmath>
#include <limits>
#include <string>

namespace score_view
{
namespace
{
// The size in tenths of a pixel, written and read by hand. Every ladder step
// has at most one decimal place, and the floating-point std::to_chars and
// std::from_chars this used first are missing from Apple's standard library
// before macOS 13.3, which the macOS build targets.
[[nodiscard]] std::string formatSize(double pixelsPerSpace)
{
    const long long tenths = std::llround(pixelsPerSpace * 10.0);
    std::string text = std::to_string(tenths / 10);

    if (tenths % 10 != 0)
    {
        text += '.';
        text += static_cast<char>('0' + (tenths % 10));
    }

    return text;
}

// Digits, then optionally a point and digits. Anything else -- a sign, an
// exponent, a stray character -- is not a size.
[[nodiscard]] std::optional<double> parseSize(std::string_view text)
{
    double value = 0.0;
    double scale = 1.0;
    bool seenPoint = false;
    bool seenDigit = false;

    for (const char character : text)
    {
        if (character == '.' && !seenPoint)
        {
            seenPoint = true;
            continue;
        }

        if (character < '0' || character > '9')
        {
            return std::nullopt;
        }

        seenDigit = true;
        const auto digit = static_cast<double>(character - '0');

        if (seenPoint)
        {
            scale /= 10.0;
            value += digit * scale;
        }
        else
        {
            value = (value * 10.0) + digit;
        }
    }

    if (!seenDigit)
    {
        return std::nullopt;
    }

    return value;
}
} // namespace

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

    std::string data = "view=";
    data += nameOf(settings.view);
    data += ";size=";
    data += formatSize(sizeLadder.at(step));

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
                const std::optional<double> size = parseSize(value);

                if (size.has_value() && *size > 0.0)
                {
                    settings.sizeStep = nearestSizeStep(*size);
                }
            }
        }

        start = end + 1;
    }

    return settings;
}
} // namespace score_view
