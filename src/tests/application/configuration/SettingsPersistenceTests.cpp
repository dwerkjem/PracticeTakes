#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/configuration/SettingsPersistence.h"
#include "application/shell/ui/workspace/model/WorkspaceBuiltIns.h"

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
    expected.midiInputDeviceIdentifier = "alsa-seq-24-0";
    expected.midiInputDeviceName = "Casio PX-S1100";
    expected.tuner = {3, 0.4, 8.0, 0.7, 6.0, 25.0};
    expected.tunerBounds = "10 20 920 760";
    expected.spectrogramBounds = "30 40 980 650";
    expected.harmonicBounds = "40 50 980 700";
    expected.settingsBounds = "50 60 900 760";
    expected.recentTool = AppSettings::RecentTool::spectrogram;
    expected.fullscreenMode = AppSettings::FullscreenMode::kiosk;
    expected.workspaceCatalog.active = WorkspaceBuiltIns::spectrumAnalysis().snapshot;
    expected.workspaceCatalog.activeSource = WorkspaceBuiltIns::spectrumAnalysis().id;

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
    CHECK(loaded.state.midiInputDeviceIdentifier == expected.midiInputDeviceIdentifier);
    CHECK(loaded.state.midiInputDeviceName == expected.midiInputDeviceName);
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
    CHECK(loaded.state.harmonicBounds == expected.harmonicBounds);
    CHECK(loaded.state.settingsBounds == expected.settingsBounds);
    CHECK(loaded.state.recentTool == expected.recentTool);
    CHECK(loaded.state.fullscreenMode == expected.fullscreenMode);
    CHECK(loaded.state.workspaceCatalog == expected.workspaceCatalog);
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

TEST_CASE(
    "schema four settings migrate into Pitch Practice without losing valid preferences",
    "[settings][migration][workspace]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile legacy(temporary.path, settingsOptions());
        legacy.setValue("settings.schema", 4);
        legacy.setValue("global.theme", static_cast<int>(Theme::dark));
        legacy.setValue("audio.muted", true);
        legacy.setValue("audio.inputGain", 0.75);
        legacy.setValue("audio.deviceState", "<DEVICESETUP deviceType=\"Test\"/>");
        legacy.setValue("tuner.displayMode", 3);
        legacy.setValue("tuner.easing", 0.45);
        legacy.setValue("tuner.averaging", 9.0);
        legacy.setValue("tuner.noteSwitch", 0.8);
        legacy.setValue("tuner.dropout", 6.0);
        legacy.setValue("tuner.graphDuration", 28.0);
        legacy.setValue("layout.tuner", "10 20 920 760");
        legacy.setValue("layout.spectrogram", "30 40 980 650");
        legacy.setValue("layout.harmonics", "40 50 980 700");
        legacy.setValue("layout.settings", "50 60 900 760");
        legacy.setValue(
            "window.fullscreenMode", static_cast<int>(AppSettings::FullscreenMode::kiosk));
        REQUIRE(legacy.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::migrated);
    CHECK(loaded.state.theme == Theme::dark);
    CHECK(loaded.state.microphoneMuted);
    CHECK(loaded.state.inputGain == Catch::Approx(0.75));
    CHECK(loaded.state.audioDeviceState == "<DEVICESETUP deviceType=\"Test\"/>");
    CHECK(loaded.state.fullscreenMode == AppSettings::FullscreenMode::kiosk);
    CHECK(loaded.state.tuner.displayMode == 3);
    CHECK(loaded.state.tuner.easing == Catch::Approx(0.45));
    CHECK(loaded.state.tuner.averaging == Catch::Approx(9.0));
    CHECK(loaded.state.tuner.noteSwitchSemitones == Catch::Approx(0.8));
    CHECK(loaded.state.tuner.dropoutFrames == Catch::Approx(6.0));
    CHECK(loaded.state.tuner.graphDurationSeconds == Catch::Approx(28.0));
    CHECK(loaded.state.tunerBounds == "10 20 920 760");
    CHECK(loaded.state.spectrogramBounds == "30 40 980 650");
    CHECK(loaded.state.harmonicBounds == "40 50 980 700");
    CHECK(loaded.state.settingsBounds == "50 60 900 760");

    const auto& catalog = loaded.state.workspaceCatalog;
    CHECK(catalog.activeSource == WorkspaceBuiltIns::pitchPractice().id);
    CHECK(catalog.active.dockRoot == WorkspaceBuiltIns::pitchPractice().snapshot.dockRoot);
    CHECK(catalog.active.floating.empty());
    CHECK(catalog.active.focusedTool == "tuner");
    CHECK(catalog.named.empty());

    const auto tunerPayload = catalog.active.toolSettings.find("tuner");
    REQUIRE(tunerPayload != catalog.active.toolSettings.end());
    REQUIRE(tunerPayload->second.version == 1);
    const auto tunerSettings = juce::JSON::parse(tunerPayload->second.data);
    REQUIRE(tunerSettings.getDynamicObject() != nullptr);
    CHECK(static_cast<int>(tunerSettings["displayMode"]) == 3);
    CHECK(static_cast<double>(tunerSettings["easing"]) == Catch::Approx(0.45));
    CHECK(static_cast<double>(tunerSettings["averaging"]) == Catch::Approx(9.0));
    CHECK(static_cast<double>(tunerSettings["noteSwitchSemitones"]) == Catch::Approx(0.8));
    CHECK(static_cast<double>(tunerSettings["dropoutFrames"]) == Catch::Approx(6.0));
    CHECK(static_cast<double>(tunerSettings["graphDurationSeconds"]) == Catch::Approx(28.0));
}

TEST_CASE(
    "schema four migration retains only valid legacy window bounds",
    "[settings][migration][workspace]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile legacy(temporary.path, settingsOptions());
        legacy.setValue("settings.schema", 4);
        legacy.setValue("layout.tuner", "10 20 920 760");
        legacy.setValue("layout.spectrogram", "30 40 299 650");
        legacy.setValue("layout.harmonics", "40 50 10001 700");
        legacy.setValue("layout.settings", "50 60 900 760");
        REQUIRE(legacy.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::migrated);
    CHECK(loaded.state.tunerBounds == "10 20 920 760");
    CHECK(loaded.state.spectrogramBounds.isEmpty());
    CHECK(loaded.state.harmonicBounds.isEmpty());
    CHECK(loaded.state.settingsBounds == "50 60 900 760");
}

TEST_CASE(
    "schema five storage owns the workspace catalog and cleans legacy layout keys",
    "[settings][migration][workspace]")
{
    juce::PropertySet properties;
    properties.setValue("layout.tuner", "legacy tuner bounds");
    properties.setValue("layout.spectrogram", "legacy spectrogram bounds");
    properties.setValue("layout.harmonics", "legacy harmonic bounds");
    properties.setValue("layout.settings", "legacy settings bounds");
    properties.setValue("layout.recentTool", 3);

    AppSettings::State state;
    state.tunerBounds = "10 20 920 760";
    state.spectrogramBounds = "30 40 980 650";
    state.harmonicBounds = "40 50 980 700";
    state.settingsBounds = "50 60 900 760";
    AppSettings::store(properties, state);

    CHECK(properties.getIntValue("settings.schema") == AppDefaults::schemaVersion);
    CHECK(properties.containsKey("workspace.catalog"));
    CHECK(properties.getValue("workspace.lastFloating.tuner") == state.tunerBounds);
    CHECK(properties.getValue("workspace.lastFloating.spectrogram") == state.spectrogramBounds);
    CHECK(properties.getValue("workspace.lastFloating.harmonicAnalyzer") == state.harmonicBounds);
    CHECK(properties.getValue("window.settingsBounds") == state.settingsBounds);
    CHECK_FALSE(properties.containsKey("layout.tuner"));
    CHECK_FALSE(properties.containsKey("layout.spectrogram"));
    CHECK_FALSE(properties.containsKey("layout.harmonics"));
    CHECK_FALSE(properties.containsKey("layout.settings"));
    CHECK_FALSE(properties.containsKey("layout.recentTool"));
}

TEST_CASE(
    "malformed workspace catalog preserves valid global settings during recovery",
    "[settings][recovery][workspace]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile invalid(temporary.path, settingsOptions());
        AppSettings::State state;
        state.theme = Theme::dark;
        state.microphoneMuted = true;
        state.inputGain = 0.75;
        state.fullscreenMode = AppSettings::FullscreenMode::kiosk;
        AppSettings::store(invalid, state);
        invalid.setValue("workspace.catalog", "{not-json");
        REQUIRE(invalid.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::recoveredFromCorruption);
    CHECK(loaded.state.theme == Theme::dark);
    CHECK(loaded.state.microphoneMuted);
    CHECK(loaded.state.inputGain == Catch::Approx(0.75));
    CHECK(loaded.state.fullscreenMode == AppSettings::FullscreenMode::kiosk);
    CHECK(loaded.state.workspaceCatalog.active == WorkspaceBuiltIns::pitchPractice().snapshot);
}

TEST_CASE(
    "newer workspace catalog versions prevent destructive native rewrites",
    "[settings][migration][workspace]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile newer(temporary.path, settingsOptions());
        AppSettings::State state;
        state.theme = Theme::dark;
        AppSettings::store(newer, state);
        newer.setValue(
            "workspace.catalog",
            R"({"version":999,"active":{"dockRoot":{"kind":"leaf","toolId":"tuner"},"floating":[],"toolSettings":{}},"named":[]})");
        REQUIRE(newer.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    CHECK(loaded.status == AppSettings::LoadStatus::newerSchema);
    CHECK(loaded.state.theme == Theme::dark);
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
        AppSettings::store(invalid, AppSettings::State{});
        invalid.setValue("global.theme", 99);
        invalid.setValue("audio.inputGain", 99.0);
        invalid.setValue("tuner.displayMode", -1);
        invalid.setValue("tuner.easing", "not-a-number");
        invalid.setValue("tuner.averaging", 100.0);
        invalid.setValue("window.fullscreenMode", 99);
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
    CHECK(loaded.state.fullscreenMode == AppSettings::FullscreenMode::normal);
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

TEST_CASE("negative settings schemas recover as corruption", "[settings][recovery]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile invalid(temporary.path, settingsOptions());
        invalid.setValue("settings.schema", -1);
        REQUIRE(invalid.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    CHECK(loaded.status == AppSettings::LoadStatus::recoveredFromCorruption);
    CHECK(loaded.state.theme == AppDefaults::theme);
}

TEST_CASE("unowned properties do not turn defaults into a migration", "[settings][migration]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile unrelated(temporary.path, settingsOptions());
        unrelated.setValue("another.application.value", "keep me");
        REQUIRE(unrelated.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    CHECK(loaded.status == AppSettings::LoadStatus::defaults);
}

TEST_CASE("validation accepts every documented tuner boundary", "[settings][validation]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile boundary(temporary.path, settingsOptions());
        AppSettings::store(boundary, AppSettings::State{});
        boundary.setValue("audio.inputGain", 0.0);
        boundary.setValue("tuner.displayMode", 3);
        boundary.setValue("tuner.easing", 0.02);
        boundary.setValue("tuner.averaging", 15.0);
        boundary.setValue("tuner.noteSwitch", 0.1);
        boundary.setValue("tuner.dropout", 20.0);
        boundary.setValue("tuner.graphDuration", 60.0);
        REQUIRE(boundary.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::loaded);
    CHECK(loaded.state.inputGain == Catch::Approx(0.0));
    CHECK(loaded.state.tuner.displayMode == 3);
    CHECK(loaded.state.tuner.easing == Catch::Approx(0.02));
    CHECK(loaded.state.tuner.averaging == Catch::Approx(15.0));
    CHECK(loaded.state.tuner.noteSwitchSemitones == Catch::Approx(0.1));
    CHECK(loaded.state.tuner.dropoutFrames == Catch::Approx(20.0));
    CHECK(loaded.state.tuner.graphDurationSeconds == Catch::Approx(60.0));
}

TEST_CASE("legacy recent-tool values do not override the workspace focus", "[settings][validation]")
{
    TemporarySettingsFile temporary;
    {
        juce::PropertiesFile invalid(temporary.path, settingsOptions());
        AppSettings::store(invalid, AppSettings::State{});
        invalid.setValue("layout.recentTool", 99);
        REQUIRE(invalid.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    REQUIRE(loaded.status == AppSettings::LoadStatus::loaded);
    CHECK(loaded.state.recentTool == AppSettings::RecentTool::tuner);
}

TEST_CASE("clearing settings removes only Practice Takes owned values", "[settings][persistence]")
{
    juce::PropertySet properties;
    properties.setValue("another.application.value", "keep me");
    AppSettings::State state;
    state.theme = Theme::dark;
    state.fullscreenMode = AppSettings::FullscreenMode::kiosk;
    AppSettings::store(properties, state);

    REQUIRE(properties.containsKey("settings.schema"));
    REQUIRE(properties.containsKey("global.theme"));
    REQUIRE(properties.containsKey("window.fullscreenMode"));

    AppSettings::clearOwnedValues(properties);

    CHECK_FALSE(properties.containsKey("settings.schema"));
    CHECK_FALSE(properties.containsKey("global.theme"));
    CHECK_FALSE(properties.containsKey("window.fullscreenMode"));
    CHECK(properties.getValue("another.application.value") == "keep me");
}

// Adding the MIDI keys bumped the schema, so every settings file in existence
// loads as `migrated`. That path has to leave a user with no MIDI device
// selected rather than with something invented, and has to keep everything
// they had already set.
TEST_CASE(
    "settings written before MIDI existed migrate with no device selected",
    "[settings][midi]")
{
    TemporarySettingsFile temporary;

    {
        juce::PropertiesFile writer(temporary.path, settingsOptions());
        writer.setValue("settings.schema", 5);
        writer.setValue("global.theme", 2);
        writer.setValue("audio.inputGain", 1.5);
        REQUIRE(writer.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    CHECK(loaded.status == AppSettings::LoadStatus::migrated);
    CHECK(loaded.state.midiInputDeviceIdentifier.isEmpty());
    CHECK(loaded.state.midiInputDeviceName.isEmpty());

    // And what was already there survived, which is the half of a migration
    // that is easy to break without noticing.
    CHECK(loaded.state.theme == Theme::dark);
    CHECK(loaded.state.inputGain == Catch::Approx(1.5));
}

TEST_CASE(
    "a remembered MIDI device name survives without its identifier resolving",
    "[settings][midi]")
{
    // The unplugged-keyboard case. Nothing here resolves the identifier against
    // real hardware -- that is the service's job -- but both values have to come
    // back, because the name is what the interface shows while the device is
    // away.
    TemporarySettingsFile temporary;
    AppSettings::State expected;
    expected.midiInputDeviceIdentifier = "a-device-that-is-not-here";
    expected.midiInputDeviceName = "Yamaha P-125";

    {
        juce::PropertiesFile writer(temporary.path, settingsOptions());
        AppSettings::store(writer, expected);
        REQUIRE(writer.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);

    CHECK(loaded.state.midiInputDeviceIdentifier == "a-device-that-is-not-here");
    CHECK(loaded.state.midiInputDeviceName == "Yamaha P-125");
}

TEST_CASE("clearing settings removes the MIDI keys too", "[settings][midi]")
{
    // A key absent from `ownedKeys` survives a reset, which would leave a user
    // who reset everything still pointed at a device.
    TemporarySettingsFile temporary;
    AppSettings::State state;
    state.midiInputDeviceIdentifier = "alsa-seq-24-0";
    state.midiInputDeviceName = "Casio PX-S1100";

    juce::PropertiesFile properties(temporary.path, settingsOptions());
    AppSettings::store(properties, state);
    REQUIRE(properties.containsKey("midi.inputDeviceIdentifier"));

    AppSettings::clearOwnedValues(properties);

    CHECK_FALSE(properties.containsKey("midi.inputDeviceIdentifier"));
    CHECK_FALSE(properties.containsKey("midi.inputDeviceName"));
}
