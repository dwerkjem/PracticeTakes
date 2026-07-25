#pragma once

enum class Theme
{
    light = 1,
    dark
};

[[nodiscard]] constexpr bool isDarkTheme(Theme theme) noexcept
{
    return theme == Theme::dark;
}
