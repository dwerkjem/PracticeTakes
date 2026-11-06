#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/configuration/AppDefaults.h"

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
