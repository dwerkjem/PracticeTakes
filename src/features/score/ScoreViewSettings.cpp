#include "ScoreViewSettings.h"

#include <algorithm>
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
// A part's name, made safe to sit between the payload's separators.
[[nodiscard]] std::string escapeName(std::string_view name)
{
    static constexpr std::string_view reserved = ":,;=%";
    static constexpr std::string_view hex = "0123456789ABCDEF";

    std::string escaped;

    for (const char character : name)
    {
        if (reserved.find(character) == std::string_view::npos)
        {
            escaped.push_back(character);
            continue;
        }

        const auto byte = static_cast<unsigned char>(character);
        escaped.push_back('%');
        escaped.push_back(hex.at(byte >> 4U));
        escaped.push_back(hex.at(byte & 0x0FU));
    }

    return escaped;
}

// The other way. Nothing for a malformed escape, so the entry is skipped
// rather than read as a different name.
[[nodiscard]] std::optional<std::string> unescapeName(std::string_view escaped)
{
    const auto digit = [](char character) -> int
    {
        if (character >= '0' && character <= '9')
        {
            return character - '0';
        }

        if (character >= 'A' && character <= 'F')
        {
            return character - 'A' + 10;
        }

        if (character >= 'a' && character <= 'f')
        {
            return character - 'a' + 10;
        }

        return -1;
    };

    std::string name;

    for (std::size_t index = 0; index < escaped.size(); ++index)
    {
        if (escaped.at(index) != '%')
        {
            name.push_back(escaped.at(index));
            continue;
        }

        // A percent needs two hex digits after it.
        if (index + 2 >= escaped.size())
        {
            return std::nullopt;
        }

        const int high = digit(escaped.at(index + 1));
        const int low = digit(escaped.at(index + 2));

        if (high < 0 || low < 0)
        {
            return std::nullopt;
        }

        name.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }

    return name;
}

// "P1:Voice,P3:Alto" into its parts. An entry with no id, no colon or a bad
// escape is skipped and the rest kept, as for every other field.
[[nodiscard]] std::vector<engraving::PartKey> parseHiddenParts(std::string_view value)
{
    std::vector<engraving::PartKey> parts;
    std::size_t start = 0;

    while (start <= value.size())
    {
        const std::size_t end = std::min(value.find(',', start), value.size());
        const std::string_view entry = value.substr(start, end - start);
        const std::size_t colon = entry.find(':');

        if (colon != std::string_view::npos && colon > 0)
        {
            if (std::optional<std::string> name = unescapeName(entry.substr(colon + 1)))
            {
                parts.push_back(engraving::PartKey{std::string(entry.substr(0, colon)), *name});
            }
        }

        start = end + 1;
    }

    return parts;
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

    if (!settings.hiddenParts.empty())
    {
        data += ";hidden=";

        for (std::size_t index = 0; index < settings.hiddenParts.size(); ++index)
        {
            const engraving::PartKey& part = settings.hiddenParts.at(index);
            data += index == 0 ? "" : ",";
            data += part.id;
            data += ':';
            data += escapeName(part.name);
        }
    }

    // Each written only when it is not the default, so a Score View nobody
    // changed saves exactly what it always did (score-cursor decision 8).
    const int ahead = std::clamp(settings.lookAhead, 0, engraving::maximumLookAhead);
    if (ahead != engraving::defaultLookAhead)
    {
        data += ";ahead=" + std::to_string(ahead);
    }
    if (!settings.follow)
    {
        data += ";follow=0";
    }
    if (settings.pageFit == PageFit::page)
    {
        data += ";fit=page";
    }
    if (!settings.showReview)
    {
        data += ";review=0";
    }

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
            else if (key == "hidden")
            {
                settings.hiddenParts = parseHiddenParts(value);
            }
            else if (key == "ahead")
            {
                if (value.size() == 1 && value.front() >= '0' && value.front() <= '9')
                {
                    settings.lookAhead = std::min(value.front() - '0', engraving::maximumLookAhead);
                }
            }
            else if (key == "follow")
            {
                settings.follow = value != "0";
            }
            else if (key == "review")
            {
                settings.showReview = value != "0";
            }
            else if (key == "fit")
            {
                settings.pageFit = value == "page" ? PageFit::page : PageFit::width;
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
