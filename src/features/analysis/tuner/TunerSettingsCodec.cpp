#include "TunerSettingsCodec.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
[[nodiscard]] std::optional<double>
boundedPayloadNumber(const juce::var& value, double minimum, double maximum)
{
    if (!value.isInt() && !value.isInt64() && !value.isDouble())
    {
        return std::nullopt;
    }

    const auto number = static_cast<double>(value);
    if (!std::isfinite(number) || number < minimum || number > maximum)
    {
        return std::nullopt;
    }
    return number;
}

[[nodiscard]] std::optional<int>
boundedPayloadInteger(const juce::var& value, int minimum, int maximum)
{
    if (!value.isInt() && !value.isInt64())
    {
        return std::nullopt;
    }

    const auto number = static_cast<int>(value);
    if (number < minimum || number > maximum)
    {
        return std::nullopt;
    }
    return number;
}
} // namespace

namespace TunerSettingsCodec
{
ToolSettingsPayload encode(const AppDefaults::TunerSettings& settings)
{
    auto* object = new juce::DynamicObject;
    object->setProperty("displayMode", settings.displayMode);
    object->setProperty("easing", settings.easing);
    object->setProperty("averaging", settings.averaging);
    object->setProperty("noteSwitchSemitones", settings.noteSwitchSemitones);
    object->setProperty("dropoutFrames", settings.dropoutFrames);
    object->setProperty("graphDurationSeconds", settings.graphDurationSeconds);
    return {1, juce::JSON::toString(juce::var(object)).toStdString()};
}

std::optional<AppDefaults::TunerSettings> decode(const std::optional<ToolSettingsPayload>& payload)
{
    if (!payload.has_value() || payload->version != 1)
    {
        return AppDefaults::tunerDefaults();
    }

    const auto decoded = juce::JSON::parse(juce::String::fromUTF8(payload->data.c_str()));
    if (decoded.getDynamicObject() == nullptr)
    {
        return std::nullopt;
    }

    const auto displayMode = boundedPayloadInteger(decoded["displayMode"], 1, 3);
    const auto easing = boundedPayloadNumber(decoded["easing"], 0.02, 1.0);
    const auto averaging = boundedPayloadNumber(decoded["averaging"], 1.0, 15.0);
    const auto noteSwitch = boundedPayloadNumber(decoded["noteSwitchSemitones"], 0.1, 1.5);
    const auto dropout = boundedPayloadNumber(decoded["dropoutFrames"], 1.0, 20.0);
    const auto duration = boundedPayloadNumber(decoded["graphDurationSeconds"], 5.0, 60.0);
    if (!displayMode.has_value() || !easing.has_value() || !averaging.has_value() ||
        !noteSwitch.has_value() || !dropout.has_value() || !duration.has_value())
    {
        return std::nullopt;
    }

    return AppDefaults::TunerSettings{
        *displayMode, *easing, *averaging, *noteSwitch, *dropout, *duration};
}
} // namespace TunerSettingsCodec
