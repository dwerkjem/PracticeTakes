#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/configuration/AppDefaults.h"

#include <array>
#include <cstddef>

TEST_CASE("General instrument preset matches the tuner defaults", "[defaults][tuner]")
{
    const auto defaults = AppDefaults::tunerDefaults();
    const auto generalInstrument = AppDefaults::tunerPreset(AppDefaults::Preset::generalInstrument);

    CHECK(defaults.displayMode == AppDefaults::Tuner::displayMode);
    CHECK(defaults.easing == Catch::Approx(AppDefaults::Tuner::easing));
    CHECK(defaults.averaging == Catch::Approx(AppDefaults::Tuner::averaging));
    CHECK(defaults.noteSwitchSemitones == Catch::Approx(AppDefaults::Tuner::noteSwitchSemitones));
    CHECK(defaults.dropoutFrames == Catch::Approx(AppDefaults::Tuner::dropoutFrames));
    CHECK(defaults.graphDurationSeconds == Catch::Approx(AppDefaults::Tuner::graphDurationSeconds));

    CHECK(generalInstrument.displayMode == defaults.displayMode);
    CHECK(generalInstrument.easing == Catch::Approx(defaults.easing));
    CHECK(generalInstrument.averaging == Catch::Approx(defaults.averaging));
    CHECK(generalInstrument.noteSwitchSemitones == Catch::Approx(defaults.noteSwitchSemitones));
    CHECK(generalInstrument.dropoutFrames == Catch::Approx(defaults.dropoutFrames));
    CHECK(generalInstrument.graphDurationSeconds == Catch::Approx(defaults.graphDurationSeconds));
}

TEST_CASE("Voice preset favors steadier pitch tracking", "[defaults][tuner]")
{
    const auto voice = AppDefaults::tunerPreset(AppDefaults::Preset::voice);
    const auto generalInstrument = AppDefaults::tunerDefaults();

    CHECK(voice.displayMode == generalInstrument.displayMode);
    CHECK(voice.easing < generalInstrument.easing);
    CHECK(voice.averaging > generalInstrument.averaging);
    CHECK(voice.noteSwitchSemitones < generalInstrument.noteSwitchSemitones);
    CHECK(voice.dropoutFrames > generalInstrument.dropoutFrames);
    CHECK(voice.graphDurationSeconds > generalInstrument.graphDurationSeconds);
}

TEST_CASE("Dark-theme detection distinguishes both themes", "[defaults][theme]")
{
    CHECK_FALSE(isDarkTheme(Theme::light));
    CHECK(isDarkTheme(Theme::dark));
}

TEST_CASE("Microphone analysis gain defaults to unity", "[defaults][audio]")
{
    CHECK(AppDefaults::Audio::inputGain == Catch::Approx(1.0));
}

TEST_CASE("Every preset is declared once, with a name and a usable id", "[defaults][tuner]")
{
    // The Settings window lists the table as it stands and uses each id as a
    // ComboBox item id: 0 is JUCE's "nothing selected", and two equal ids
    // would make one entry unreachable.
    for (std::size_t i = 0; i < AppDefaults::presets.size(); ++i)
    {
        const AppDefaults::PresetDefinition& preset = AppDefaults::presets[i];
        INFO(preset.label);

        CHECK_FALSE(preset.label.empty());
        CHECK(static_cast<int>(preset.id) > 0);

        for (std::size_t j = i + 1; j < AppDefaults::presets.size(); ++j)
        {
            CHECK(AppDefaults::presets[j].id != preset.id);
            CHECK(AppDefaults::presets[j].label != preset.label);
        }
    }
}

TEST_CASE("The preset table keeps the names and order the Settings window had", "[defaults][tuner]")
{
    // A refactor's pin: the list moved out of the window unchanged.
    REQUIRE(AppDefaults::presets.size() == 2);
    CHECK(AppDefaults::presets[0].id == AppDefaults::Preset::voice);
    CHECK(AppDefaults::presets[0].label == "Voice practice");
    CHECK(AppDefaults::presets[1].id == AppDefaults::Preset::generalInstrument);
    CHECK(AppDefaults::presets[1].label == "General instrument");
}

TEST_CASE("The voice preset keeps its values", "[defaults][tuner]")
{
    const auto voice = AppDefaults::tunerPreset(AppDefaults::Preset::voice);

    CHECK(voice.displayMode == AppDefaults::Tuner::displayMode);
    CHECK(voice.easing == Catch::Approx(0.25));
    CHECK(voice.averaging == Catch::Approx(7.0));
    CHECK(voice.noteSwitchSemitones == Catch::Approx(0.45));
    CHECK(voice.dropoutFrames == Catch::Approx(7.0));
    CHECK(voice.graphDurationSeconds == Catch::Approx(30.0));
}

TEST_CASE("An unknown preset id applies the defaults", "[defaults][tuner]")
{
    const auto unknown = AppDefaults::tunerPreset(static_cast<AppDefaults::Preset>(99));

    CHECK(unknown.easing == Catch::Approx(AppDefaults::tunerDefaults().easing));
    CHECK(unknown.averaging == Catch::Approx(AppDefaults::tunerDefaults().averaging));
}

TEST_CASE("The tuner schema covers every numeric setting once", "[defaults][tuner]")
{
    // TunerSettings has ten numeric members besides displayMode; an eleventh
    // with no schema entry would be written by no codec and checked by no
    // reader.
    static_assert(sizeof(AppDefaults::TunerSettings) == sizeof(double) * 11);
    REQUIRE(AppDefaults::tunerNumberFields.size() == 10);

    for (std::size_t i = 0; i < AppDefaults::tunerNumberFields.size(); ++i)
    {
        const AppDefaults::TunerNumberField& field = AppDefaults::tunerNumberFields[i];
        INFO(field.key);

        CHECK(&AppDefaults::tunerField(field.member) == &field);
        CHECK(field.minimum < field.maximum);
        CHECK(field.step > 0.0);

        for (std::size_t j = i + 1; j < AppDefaults::tunerNumberFields.size(); ++j)
        {
            CHECK(AppDefaults::tunerNumberFields[j].member != field.member);
            CHECK(AppDefaults::tunerNumberFields[j].key != field.key);
        }
    }
}

TEST_CASE("The tuner schema keeps the ranges each reader had", "[defaults][tuner]")
{
    // A refactor's pin: the codec, the settings migration and the sliders
    // each spelled these out before the schema did.
    using AppDefaults::TunerSettings;
    const auto range = [](double TunerSettings::*member)
    {
        const auto& field = AppDefaults::tunerField(member);
        return std::array{field.minimum, field.maximum, field.step};
    };

    CHECK(range(&TunerSettings::easing) == std::array{0.02, 1.0, 0.01});
    CHECK(range(&TunerSettings::averaging) == std::array{1.0, 15.0, 1.0});
    CHECK(range(&TunerSettings::noteSwitchSemitones) == std::array{0.1, 1.5, 0.05});
    CHECK(range(&TunerSettings::dropoutFrames) == std::array{1.0, 20.0, 1.0});
    CHECK(range(&TunerSettings::graphDurationSeconds) == std::array{5.0, 60.0, 1.0});
    CHECK(range(&TunerSettings::scaleCents) == std::array{10.0, 100.0, 5.0});
    CHECK_FALSE(AppDefaults::tunerField(&TunerSettings::scaleCents).required);
    CHECK(range(&TunerSettings::signs) == std::array{0.0, 2.0, 1.0});
    CHECK(range(&TunerSettings::bothHalvesLit) == std::array{0.0, 1.0, 1.0});
    CHECK_FALSE(AppDefaults::tunerField(&TunerSettings::signs).required);
    CHECK_FALSE(AppDefaults::tunerField(&TunerSettings::bothHalvesLit).required);
    CHECK(range(&TunerSettings::marker) == std::array{0.0, 6.0, 1.0});
    CHECK_FALSE(AppDefaults::tunerField(&TunerSettings::marker).required);
    CHECK(range(&TunerSettings::offsetShown) == std::array{0.0, 1.0, 1.0});
    CHECK_FALSE(AppDefaults::tunerField(&TunerSettings::offsetShown).required);
    CHECK(AppDefaults::tunerDisplayModeMinimum == 1);
    CHECK(AppDefaults::tunerDisplayModeMaximum == 3);
}

TEST_CASE("Every preset is inside the tuner schema's ranges", "[defaults][tuner]")
{
    // A preset outside a range would apply, then fail to restore: the codec
    // refuses the payload it was saved as.
    for (const AppDefaults::PresetDefinition& preset : AppDefaults::presets)
    {
        INFO(preset.label);
        CHECK(preset.tuner.displayMode >= AppDefaults::tunerDisplayModeMinimum);
        CHECK(preset.tuner.displayMode <= AppDefaults::tunerDisplayModeMaximum);

        for (const AppDefaults::TunerNumberField& field : AppDefaults::tunerNumberFields)
        {
            INFO(field.key);
            CHECK(preset.tuner.*field.member >= field.minimum);
            CHECK(preset.tuner.*field.member <= field.maximum);
        }
    }
}
