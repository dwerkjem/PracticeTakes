#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/analysis/tuner/TunerSettingsCodec.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

// The tuner's workspace payload, read and written against the schema in
// AppDefaults::tunerNumberFields.
using AppDefaults::TunerSettings;
using Catch::Approx;

namespace
{
[[nodiscard]] juce::var parsed(const ToolSettingsPayload& payload)
{
    return juce::JSON::parse(juce::String(payload.data));
}

[[nodiscard]] ToolSettingsPayload withProperty(const char* key, const juce::var& value)
{
    juce::var object = parsed(TunerSettingsCodec::encode(AppDefaults::tunerDefaults()));
    object.getDynamicObject()->setProperty(key, value);
    return {1, juce::JSON::toString(object).toStdString()};
}
} // namespace

TEST_CASE("A tuner payload round-trips", "[tuner][codec]")
{
    const TunerSettings voice = AppDefaults::tunerPreset(AppDefaults::Preset::voice);

    const auto decoded = TunerSettingsCodec::decode(TunerSettingsCodec::encode(voice));

    REQUIRE(decoded.has_value());
    CHECK(decoded->displayMode == voice.displayMode);
    for (const AppDefaults::TunerNumberField& field : AppDefaults::tunerNumberFields)
    {
        INFO(field.key);
        CHECK((*decoded).*field.member == Approx(voice.*field.member));
    }
}

TEST_CASE("A tuner payload keeps the names and order it was always written in", "[tuner][codec]")
{
    // Saved workspaces hold these names; renaming one would make every saved
    // tuner fail to restore.
    const juce::var object = parsed(TunerSettingsCodec::encode(AppDefaults::tunerDefaults()));
    REQUIRE(object.getDynamicObject() != nullptr);

    std::vector<std::string> names;
    for (const auto& property : object.getDynamicObject()->getProperties())
    {
        names.push_back(property.name.toString().toStdString());
    }

    CHECK(
        names == std::vector<std::string>{
                     "displayMode", "easing", "averaging", "noteSwitchSemitones", "dropoutFrames",
                     "graphDurationSeconds", "scaleCents", "signs", "bothHalvesLit", "marker"});
}

TEST_CASE("A tuner payload with a value outside its range is refused", "[tuner][codec]")
{
    for (const AppDefaults::TunerNumberField& field : AppDefaults::tunerNumberFields)
    {
        const std::string key(field.key);
        INFO(key);

        CHECK(TunerSettingsCodec::decode(withProperty(key.c_str(), field.minimum)).has_value());
        CHECK(TunerSettingsCodec::decode(withProperty(key.c_str(), field.maximum)).has_value());
        CHECK_FALSE(TunerSettingsCodec::decode(withProperty(key.c_str(), field.minimum - 0.001))
                        .has_value());
        CHECK_FALSE(TunerSettingsCodec::decode(withProperty(key.c_str(), field.maximum + 0.001))
                        .has_value());
        CHECK_FALSE(TunerSettingsCodec::decode(withProperty(key.c_str(), "fast")).has_value());
    }

    CHECK(TunerSettingsCodec::decode(withProperty("displayMode", 3)).has_value());
    CHECK_FALSE(TunerSettingsCodec::decode(withProperty("displayMode", 0)).has_value());
    CHECK_FALSE(TunerSettingsCodec::decode(withProperty("displayMode", 4)).has_value());
    CHECK_FALSE(TunerSettingsCodec::decode(withProperty("displayMode", 1.5)).has_value());
}

TEST_CASE("A tuner payload missing a required field is refused", "[tuner][codec]")
{
    for (const AppDefaults::TunerNumberField& field : AppDefaults::tunerNumberFields)
    {
        INFO(field.key);
        if (!field.required)
        {
            continue;
        }

        juce::var object = parsed(TunerSettingsCodec::encode(AppDefaults::tunerDefaults()));
        object.getDynamicObject()->removeProperty(
            juce::Identifier(juce::String(field.key.data(), field.key.size())));

        CHECK_FALSE(TunerSettingsCodec::decode(
                        ToolSettingsPayload{1, juce::JSON::toString(object).toStdString()})
                        .has_value());
    }
}

TEST_CASE(
    "A 0.6.0 tuner payload, with no scale, restores with the one-note scale",
    "[tuner][codec]")
{
    // Every field 0.6.0 wrote, none of them the default, and no scaleCents.
    const auto decoded = TunerSettingsCodec::decode(ToolSettingsPayload{
        1, R"({"displayMode":2,"easing":0.4,"averaging":8,"noteSwitchSemitones":0.7,)"
           R"("dropoutFrames":6,"graphDurationSeconds":25})"});

    REQUIRE(decoded.has_value());
    CHECK(decoded->displayMode == 2);
    CHECK(decoded->averaging == Approx(8.0));
    CHECK(decoded->graphDurationSeconds == Approx(25.0));
    CHECK(decoded->scaleCents == Approx(AppDefaults::Tuner::scaleCents));
    CHECK(AppDefaults::Tuner::scaleCents == Approx(50.0));

    // Signs on, one half lit: the look 0.7.0 introduces, by default.
    CHECK(decoded->signs == Approx(1.0));
    CHECK(decoded->bothHalvesLit == Approx(0.0));
    CHECK(decoded->marker == Approx(1.0));
}

TEST_CASE("A tuner's scale, signs, halves and marker survive a round trip", "[tuner][codec]")
{
    AppDefaults::TunerSettings settings = AppDefaults::tunerDefaults();
    settings.scaleCents = 25.0;
    settings.signs = 2.0;
    settings.bothHalvesLit = 1.0;
    settings.marker = 5.0;

    const auto decoded = TunerSettingsCodec::decode(TunerSettingsCodec::encode(settings));
    REQUIRE(decoded.has_value());
    CHECK(decoded->scaleCents == Approx(25.0));
    CHECK(decoded->signs == Approx(2.0));
    CHECK(decoded->bothHalvesLit == Approx(1.0));
    CHECK(decoded->marker == Approx(5.0));
}

TEST_CASE("A missing or other-version tuner payload gives the defaults", "[tuner][codec]")
{
    // As opposed to a refusal: that tuner was never configured.
    const auto none = TunerSettingsCodec::decode(std::nullopt);
    const auto other = TunerSettingsCodec::decode(ToolSettingsPayload{2, "not json"});

    REQUIRE(none.has_value());
    REQUIRE(other.has_value());
    CHECK(none->easing == Approx(AppDefaults::tunerDefaults().easing));
    CHECK(other->graphDurationSeconds == Approx(AppDefaults::tunerDefaults().graphDurationSeconds));
    CHECK_FALSE(TunerSettingsCodec::decode(ToolSettingsPayload{1, "not json"}).has_value());
}
