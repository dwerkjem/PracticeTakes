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
    for (const AppDefaults::TunerNumberField& field : AppDefaults::tunerNumberFields)
    {
        object->setProperty(
            juce::Identifier(juce::String(field.key.data(), field.key.size())),
            settings.*field.member);
    }
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

    const auto displayMode = boundedPayloadInteger(
        decoded["displayMode"], AppDefaults::tunerDisplayModeMinimum,
        AppDefaults::tunerDisplayModeMaximum);
    if (!displayMode.has_value())
    {
        return std::nullopt;
    }

    AppDefaults::TunerSettings settings = AppDefaults::tunerDefaults();
    settings.displayMode = *displayMode;
    for (const AppDefaults::TunerNumberField& field : AppDefaults::tunerNumberFields)
    {
        const juce::Identifier key(juce::String(field.key.data(), field.key.size()));
        if (!field.required && !decoded.getDynamicObject()->hasProperty(key))
        {
            continue;
        }
        const auto value = boundedPayloadNumber(decoded[key], field.minimum, field.maximum);
        if (!value.has_value())
        {
            return std::nullopt;
        }
        settings.*field.member = *value;
    }
    return settings;
}
} // namespace TunerSettingsCodec
