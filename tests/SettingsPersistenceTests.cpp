#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/SettingsPersistence.h"

namespace
{
[[nodiscard]] juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "PracticeTakesTests";
    options.filenameSuffix = ".settings";
    options.folderName = "PracticeTakesTests";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = -1;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

class TemporarySettingsFile final
{
  public:
    TemporarySettingsFile() : path(juce::File::createTempFile(".settings")) {}

    ~TemporarySettingsFile()
    {
        path.deleteFile();
    }

    juce::File path;
};
} // namespace

TEST_CASE("normal settings survive an atomic file round trip", "[settings][persistence]")
{
    TemporarySettingsFile temporary;
    AppSettings::State expected;
    expected.theme = Theme::dark;
    expected.microphoneMuted = true;
    expected.inputGain = 1.25;
    expected.audioDeviceState =
        R"(<DEVICESETUP deviceType="Test" audioInputDeviceName="Missing microphone"/>)";
    expected.tuner = {3, 0.4, 8.0, 0.7, 6.0, 25.0};
    expected.tunerBounds = "10 20 920 760";
    expected.spectrogramBounds = "30 40 980 650";
    expected.settingsBounds = "50 60 900 760";
    expected.recentTool = AppSettings::RecentTool::spectrogram;

    {
        juce::PropertiesFile writer(temporary.path, settingsOptions());
        AppSettings::store(writer, expected);
        REQUIRE(writer.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::loaded);
    CHECK(loaded.state.theme == expected.theme);
    CHECK(loaded.state.microphoneMuted);
    CHECK(loaded.state.inputGain == Catch::Approx(expected.inputGain));
    CHECK(loaded.state.audioDeviceState == expected.audioDeviceState);
    CHECK(loaded.state.tuner.displayMode == expected.tuner.displayMode);
    CHECK(loaded.state.tuner.easing == Catch::Approx(expected.tuner.easing));
    CHECK(loaded.state.tuner.averaging == Catch::Approx(expected.tuner.averaging));
    CHECK(
        loaded.state.tuner.noteSwitchSemitones ==
        Catch::Approx(expected.tuner.noteSwitchSemitones));
    CHECK(loaded.state.tuner.dropoutFrames == Catch::Approx(expected.tuner.dropoutFrames));
    CHECK(
        loaded.state.tuner.graphDurationSeconds ==
        Catch::Approx(expected.tuner.graphDurationSeconds));
    CHECK(loaded.state.tunerBounds == expected.tunerBounds);
    CHECK(loaded.state.spectrogramBounds == expected.spectrogramBounds);
    CHECK(loaded.state.settingsBounds == expected.settingsBounds);
    CHECK(loaded.state.recentTool == expected.recentTool);
}

TEST_CASE("schema one settings migrate with safe defaults for new fields", "[settings][migration]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile legacy(temporary.path, settingsOptions());
        legacy.setValue("settings.schema", 1);
        legacy.setValue("global.theme", static_cast<int>(Theme::dark));
        legacy.setValue("audio.inputGain", 0.75);
        legacy.setValue("tuner.easing", 0.5);
        legacy.setValue("layout.tuner", "10 20 920 760");
        REQUIRE(legacy.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::migrated);
    CHECK(loaded.state.theme == Theme::dark);
    CHECK_FALSE(loaded.state.microphoneMuted);
    CHECK(loaded.state.inputGain == Catch::Approx(0.75));
    CHECK(loaded.state.tuner.displayMode == AppDefaults::Tuner::displayMode);
    CHECK(loaded.state.tuner.easing == Catch::Approx(0.5));
    CHECK(loaded.state.tunerBounds == "10 20 920 760");
}

TEST_CASE("corrupt settings recover to defaults", "[settings][recovery]")
{
    TemporarySettingsFile temporary;
    REQUIRE(temporary.path.replaceWithText("not a valid Practice Takes settings file"));

    juce::PropertiesFile corrupt(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(corrupt);

    REQUIRE(loaded.status == AppSettings::LoadStatus::recoveredFromCorruption);
    CHECK(loaded.state.theme == AppDefaults::theme);
    CHECK_FALSE(loaded.state.microphoneMuted);
    CHECK(loaded.state.inputGain == Catch::Approx(AppDefaults::Audio::inputGain));
    CHECK(loaded.state.tuner.displayMode == AppDefaults::Tuner::displayMode);
}

TEST_CASE("invalid individual values fall back without rejecting the file", "[settings][recovery]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile invalid(temporary.path, settingsOptions());
        invalid.setValue("settings.schema", AppDefaults::schemaVersion);
        invalid.setValue("global.theme", 99);
        invalid.setValue("audio.inputGain", 99.0);
        invalid.setValue("tuner.displayMode", -1);
        invalid.setValue("tuner.easing", "not-a-number");
        invalid.setValue("tuner.averaging", 100.0);
        REQUIRE(invalid.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::loaded);
    CHECK(loaded.state.theme == AppDefaults::theme);
    CHECK(loaded.state.inputGain == Catch::Approx(AppDefaults::Audio::inputGain));
    CHECK(loaded.state.tuner.displayMode == AppDefaults::Tuner::displayMode);
    CHECK(loaded.state.tuner.easing == Catch::Approx(AppDefaults::Tuner::easing));
    CHECK(loaded.state.tuner.averaging == Catch::Approx(AppDefaults::Tuner::averaging));
}

TEST_CASE("newer settings schemas are left untouched", "[settings][migration]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile newer(temporary.path, settingsOptions());
        newer.setValue("settings.schema", AppDefaults::schemaVersion + 1);
        newer.setValue("global.theme", static_cast<int>(Theme::dark));
        REQUIRE(newer.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    CHECK(loaded.status == AppSettings::LoadStatus::newerSchema);
    CHECK(loaded.state.theme == AppDefaults::theme);
}
