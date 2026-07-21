#include "app/Theme.h"

AppPalette appPaletteFor(Theme theme)
{
    if (isDarkTheme(theme))
    {
        return {juce::Colour::fromRGB(18, 20, 27),    juce::Colour::fromRGB(30, 33, 42),
                juce::Colour::fromRGB(54, 59, 72),    juce::Colour::fromRGB(70, 76, 92),
                juce::Colour::fromRGB(238, 241, 247), juce::Colour::fromRGB(158, 166, 181),
                juce::Colour::fromRGB(78, 85, 103),   juce::Colour::fromRGB(100, 170, 255),
                juce::Colour::fromRGB(244, 178, 73),  juce::Colour::fromRGB(8, 9, 12),
                juce::Colour::fromRGB(30, 33, 42)};
    }

    return {juce::Colour::fromRGB(235, 236, 238), juce::Colour::fromRGB(250, 250, 251),
            juce::Colour::fromRGB(244, 244, 245), juce::Colour::fromRGB(225, 228, 233),
            juce::Colour::fromRGB(28, 31, 37),    juce::Colour::fromRGB(92, 98, 108),
            juce::Colour::fromRGB(165, 169, 178), juce::Colour::fromRGB(55, 112, 196),
            juce::Colour::fromRGB(172, 103, 18),  juce::Colour::fromRGB(38, 39, 42),
            juce::Colour::fromRGB(160, 162, 167)};
}

TunerPalette tunerPaletteFor(Theme theme)
{
    if (isDarkTheme(theme))
    {
        return {juce::Colour::fromRGB(18, 20, 27),    juce::Colour::fromRGB(25, 28, 37),
                juce::Colour::fromRGB(54, 59, 72),    juce::Colour::fromRGB(58, 65, 82),
                juce::Colour::fromRGB(238, 241, 247), juce::Colour::fromRGB(142, 150, 166),
                juce::Colour::fromRGB(100, 170, 255), juce::Colour::fromRGB(85, 214, 136)};
    }

    return {juce::Colour::fromRGB(235, 236, 238), juce::Colour::fromRGB(250, 250, 251),
            juce::Colour::fromRGB(220, 224, 230), juce::Colour::fromRGB(165, 169, 178),
            juce::Colour::fromRGB(28, 31, 37),    juce::Colour::fromRGB(92, 98, 108),
            juce::Colour::fromRGB(55, 112, 196),  juce::Colour::fromRGB(35, 145, 82)};
}

SpectrogramPalette spectrogramPaletteFor(Theme theme)
{
    if (isDarkTheme(theme))
    {
        return {juce::Colour::fromRGB(18, 20, 27), juce::Colour::fromRGB(25, 28, 37),
                juce::Colour::fromRGB(188, 194, 207), juce::Colour::fromRGB(58, 65, 82)};
    }

    return {juce::Colour::fromRGB(235, 236, 238), juce::Colour::fromRGB(250, 250, 251),
            juce::Colour::fromRGB(82, 88, 99), juce::Colour::fromRGB(165, 169, 178)};
}

juce::Colour spectrogramColourForLevel(Theme theme, float level)
{
    const auto clippedLevel = juce::jlimit(0.0f, 1.0f, level);
    if (isDarkTheme(theme))
    {
        return juce::Colour::fromHSV(juce::jmap(clippedLevel, 0.0f, 1.0f, 0.72f, 0.0f),
                                     juce::jmap(clippedLevel, 0.0f, 1.0f, 0.45f, 1.0f),
                                     juce::jmap(clippedLevel, 0.0f, 1.0f, 0.08f, 1.0f), 1.0f);
    }

    const auto quiet = juce::Colour::fromRGB(246, 248, 252);
    const auto middle = juce::Colour::fromRGB(85, 139, 214);
    const auto loud = juce::Colour::fromRGB(208, 66, 53);
    constexpr float middlePoint = 0.62f;

    return clippedLevel < middlePoint
               ? quiet.interpolatedWith(middle, clippedLevel / middlePoint)
               : middle.interpolatedWith(loud, (clippedLevel - middlePoint) / (1.0f - middlePoint));
}
