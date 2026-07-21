#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/AppDefaults.h"

TEST_CASE("General instrument preset matches the tuner defaults", "[defaults][tuner]")
{
    const auto defaults = AppDefaults::tunerDefaults();
    const auto generalInstrument = AppDefaults::tunerPreset(AppDefaults::Preset::generalInstrument);

    CHECK(defaults.easing == Catch::Approx(AppDefaults::Tuner::easing));
    CHECK(defaults.averaging == Catch::Approx(AppDefaults::Tuner::averaging));
    CHECK(defaults.noteSwitchSemitones == Catch::Approx(AppDefaults::Tuner::noteSwitchSemitones));
    CHECK(defaults.dropoutFrames == Catch::Approx(AppDefaults::Tuner::dropoutFrames));
    CHECK(defaults.graphDurationSeconds == Catch::Approx(AppDefaults::Tuner::graphDurationSeconds));

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
