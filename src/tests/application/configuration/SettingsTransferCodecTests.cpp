#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/configuration/SettingsImportPersistence.h"
#include "application/configuration/SettingsImportTransaction.h"
#include "application/configuration/SettingsTransferCodec.h"
#include "application/configuration/SettingsTransferFile.h"
#include "application/shell/ui/workspace/model/WorkspaceBuiltIns.h"

namespace
{
[[nodiscard]] juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "PracticeTakesTransferTests";
    options.filenameSuffix = ".settings";
    options.folderName = "PracticeTakesTransferTests";
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

class TemporaryTransferDirectory final
{
  public:
    TemporaryTransferDirectory()
        : path(juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getNonexistentChildFile("practice-takes-transfer", {}, true))
    {
        REQUIRE(path.createDirectory().wasOk());
    }

    ~TemporaryTransferDirectory()
    {
        path.deleteRecursively();
    }

    juce::File path;
};

[[nodiscard]] std::string encodedDocument(SettingsTransferModel model = {})
{
    model.applicationVersion = "decoder-test";
    const auto encoded = SettingsTransferCodec::encode(model);
    REQUIRE(encoded.status == SettingsTransferEncodeStatus::encoded);
    REQUIRE(encoded.document.has_value());
    return *encoded.document;
}

[[nodiscard]] juce::var parsedDocument(const std::string& document)
{
    auto parsed = juce::JSON::parse(juce::String::fromUTF8(document.c_str()));
    REQUIRE(parsed.getDynamicObject() != nullptr);
    return parsed;
}

[[nodiscard]] std::string serializedDocument(const juce::var& document)
{
    return juce::JSON::toString(document, true, 17).toStdString();
}
} // namespace

TEST_CASE(
    "settings transfer codec has a deterministic ptsettings round trip",
    "[settings][transfer]")
{
    SettingsTransferModel expected;
    expected.applicationVersion = "1.2.3-test";
    expected.feedbackInvitationsDisabled = true;
    expected.state.theme = Theme::dark;
    expected.state.microphoneMuted = true;
    expected.state.inputGain = 1.25;
    expected.state.audioDeviceState =
        R"(<DEVICESETUP deviceType=\"Test\" audioInputDeviceName=\"Mic\"/>)";
    expected.state.fullscreenMode = AppSettings::FullscreenMode::kiosk;
    expected.state.tunerBounds = "10 20 920 760";
    expected.state.spectrogramBounds = "30 40 980 650";
    expected.state.harmonicBounds = "40 50 980 700";
    expected.state.settingsBounds = "50 60 900 760";

    WorkspaceCatalog catalog;
    catalog.active = WorkspaceBuiltIns::performancePreparation().snapshot;
    catalog.activeSource = "saved-performance";
    catalog.named.push_back(
        {"saved-performance", "Saved Performance",
         WorkspaceBuiltIns::performancePreparation().snapshot});
    catalog.named.push_back(
        {"saved-spectrum", "Saved Spectrum",
         WorkspaceSnapshot{
             WorkspaceNode::leaf("spectrogram"),
             {{"tuner", {1200, 80, 880, 640}}},
             "tuner",
             {{"tuner", {1, R"({\"displayMode\":3,\"easing\":0.28})"}}},
         }});
    expected.state.workspaceCatalog = catalog;
    const auto first = SettingsTransferCodec::encode(expected);
    const auto second = SettingsTransferCodec::encode(expected);

    REQUIRE(first.status == SettingsTransferEncodeStatus::encoded);
    REQUIRE(first.document.has_value());
    REQUIRE(second.status == SettingsTransferEncodeStatus::encoded);
    REQUIRE(second.document.has_value());
    CHECK(*first.document == *second.document);

    const auto decoded = SettingsTransferCodec::decode(*first.document, {{0, 0, 2560, 1440}});

    REQUIRE(decoded.status == SettingsTransferDecodeStatus::loaded);
    REQUIRE(decoded.model.has_value());
    CHECK(decoded.model->schemaVersion == SettingsTransferModel::currentSchemaVersion);
    CHECK(decoded.model->applicationVersion == expected.applicationVersion);
    CHECK(decoded.model->feedbackInvitationsDisabled == expected.feedbackInvitationsDisabled);
    CHECK(decoded.model->state.theme == expected.state.theme);
    CHECK(decoded.model->state.microphoneMuted == expected.state.microphoneMuted);
    CHECK(decoded.model->state.inputGain == Catch::Approx(expected.state.inputGain));
    CHECK(decoded.model->state.audioDeviceState == expected.state.audioDeviceState);
    CHECK(decoded.model->state.fullscreenMode == expected.state.fullscreenMode);
    CHECK(decoded.model->state.tunerBounds == expected.state.tunerBounds);
    CHECK(decoded.model->state.spectrogramBounds == expected.state.spectrogramBounds);
    CHECK(decoded.model->state.harmonicBounds == expected.state.harmonicBounds);
    CHECK(decoded.model->state.settingsBounds == expected.state.settingsBounds);
    CHECK(decoded.model->state.workspaceCatalog == expected.state.workspaceCatalog);
}

TEST_CASE(
    "settings transfer export excludes private and operational native properties",
    "[settings][transfer][privacy]")
{
    constexpr auto excludedSentinel = "PRIVATE-NATIVE-SENTINEL-8f6f36";
    TemporarySettingsFile temporary;

    {
        juce::PropertiesFile writer(temporary.path, settingsOptions());
        AppSettings::State state;
        state.theme = Theme::dark;
        AppSettings::store(writer, state);
        writer.setValue("feedback.draft.description", excludedSentinel);
        writer.setValue("feedback.draft.email", excludedSentinel);
        writer.setValue("feedback.installationId", excludedSentinel);
        writer.setValue("feedback.successfulToolUses", excludedSentinel);
        writer.setValue("feedback.invitationShown", excludedSentinel);
        writer.setValue("native.unknownProperty", excludedSentinel);
        writer.setValue("logs.lastExport", excludedSentinel);
        writer.setValue("benchmarks.lastResult", excludedSentinel);
        writer.setValue("analysis.history", excludedSentinel);
        writer.setValue("feedback.invitationsDisabled", true);
        REQUIRE(writer.saveIfNeeded());
    }

    juce::PropertiesFile reader(temporary.path, settingsOptions());
    const auto loaded = AppSettings::load(reader);
    REQUIRE(loaded.status == AppSettings::LoadStatus::loaded);

    SettingsTransferModel model;
    model.applicationVersion = "privacy-test";
    model.state = loaded.state;
    model.feedbackInvitationsDisabled = reader.getBoolValue("feedback.invitationsDisabled", false);

    const auto encoded = SettingsTransferCodec::encode(model);

    REQUIRE(encoded.status == SettingsTransferEncodeStatus::encoded);
    REQUIRE(encoded.document.has_value());
    const auto& document = *encoded.document;
    CHECK(document.find(excludedSentinel) == std::string::npos);
    CHECK(document.find("feedback.draft") == std::string::npos);
    CHECK(document.find("feedback.installationId") == std::string::npos);
    CHECK(document.find("feedback.successfulToolUses") == std::string::npos);
    CHECK(document.find("feedback.invitationShown") == std::string::npos);
    CHECK(document.find("native.unknownProperty") == std::string::npos);
    CHECK(document.find("logs.lastExport") == std::string::npos);
    CHECK(document.find("benchmarks.lastResult") == std::string::npos);
    CHECK(document.find("analysis.history") == std::string::npos);
    CHECK(document.find("\"invitationsDisabled\": true") != std::string::npos);
}

TEST_CASE("settings transfer encoding rejects oversized output", "[settings][transfer]")
{
    SettingsTransferModel model;
    model.applicationVersion.assign(SettingsTransferCodec::maximumDocumentBytes, 'x');

    const auto encoded = SettingsTransferCodec::encode(model);

    CHECK(encoded.status == SettingsTransferEncodeStatus::tooLarge);
    CHECK_FALSE(encoded.document.has_value());
    CHECK_FALSE(encoded.error.empty());
}

TEST_CASE("settings transfer file export cancellation performs no write", "[settings][transfer]")
{
    TemporaryTransferDirectory temporary;

    const auto result = SettingsTransferFile::exportDocument("new settings", {});

    CHECK(result.status == SettingsTransferExportStatus::cancelled);
    CHECK(temporary.path.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0);
}

TEST_CASE("settings transfer file export cleans up after write failure", "[settings][transfer]")
{
    TemporaryTransferDirectory temporary;
    const auto blockedParent = temporary.path.getChildFile("not-a-directory");
    REQUIRE(blockedParent.replaceWithText("blocker"));
    const auto destination = blockedParent.getChildFile("settings.ptsettings");

    const auto result = SettingsTransferFile::exportDocument("new settings", destination);

    CHECK(result.status == SettingsTransferExportStatus::ioError);
    CHECK_FALSE(result.error.empty());
    CHECK(blockedParent.loadFileAsString() == "blocker");
    CHECK(temporary.path.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
}

TEST_CASE(
    "settings transfer file export atomically replaces the complete destination",
    "[settings][transfer]")
{
    TemporaryTransferDirectory temporary;
    const auto destination = temporary.path.getChildFile("settings.ptsettings");
    REQUIRE(destination.replaceWithText("old settings"));

    const auto result = SettingsTransferFile::exportDocument("complete new settings", destination);

    CHECK(result.status == SettingsTransferExportStatus::exported);
    CHECK(result.error.empty());
    CHECK(destination.loadFileAsString() == "complete new settings");
    CHECK(temporary.path.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 1);
}

TEST_CASE(
    "settings import transaction does nothing before confirmation",
    "[settings][transfer][transaction]")
{
    int callbackCount = 0;
    SettingsImportTransaction::Callbacks callbacks{
        [&]
        {
            ++callbackCount;
            return SettingsTransferModel{};
        },
        [&](const SettingsTransferModel&)
        {
            ++callbackCount;
            return true;
        },
        [&](const SettingsTransferModel&)
        {
            ++callbackCount;
            return true;
        }};

    bool automaticSaveEnabled = true;
    const auto result = SettingsImportTransaction::execute(
        SettingsTransferModel{}, false, automaticSaveEnabled, callbacks);

    CHECK(result.status == SettingsImportStatus::cancelled);
    CHECK(callbackCount == 0);
}

TEST_CASE(
    "settings import staging maps file and document outcomes without applying state",
    "[settings][transfer][transaction][stage]")
{
    const auto cancelled = SettingsImportTransaction::stage({}, {});
    CHECK(cancelled.status == SettingsImportStageStatus::cancelled);
    CHECK_FALSE(cancelled.model.has_value());

    TemporaryTransferDirectory temporary;
    const auto missing =
        SettingsImportTransaction::stage(temporary.path.getChildFile("missing.ptsettings"), {});
    CHECK(missing.status == SettingsImportStageStatus::ioError);
    CHECK_FALSE(missing.error.empty());

    const auto malformedFile = temporary.path.getChildFile("malformed.ptsettings");
    REQUIRE(malformedFile.replaceWithText("{not-json"));
    const auto malformed = SettingsImportTransaction::stage(malformedFile, {});
    CHECK(malformed.status == SettingsImportStageStatus::invalid);
    CHECK_FALSE(malformed.model.has_value());

    auto futureDocument = parsedDocument(encodedDocument());
    futureDocument.getDynamicObject()->setProperty("schemaVersion", 999);
    const auto futureFile = temporary.path.getChildFile("future.ptsettings");
    REQUIRE(futureFile.replaceWithText(serializedDocument(futureDocument)));
    const auto future = SettingsImportTransaction::stage(futureFile, {});
    CHECK(future.status == SettingsImportStageStatus::newerSchema);
    CHECK_FALSE(future.error.empty());

    const auto oversizedFile = temporary.path.getChildFile("oversized.ptsettings");
    REQUIRE(oversizedFile.replaceWithText(juce::String::repeatedString(
        "x", static_cast<int>(SettingsTransferCodec::maximumDocumentBytes + 1))));
    const auto oversized = SettingsImportTransaction::stage(oversizedFile, {});
    CHECK(oversized.status == SettingsImportStageStatus::tooLarge);
    CHECK_FALSE(oversized.model.has_value());

    SettingsTransferModel expected;
    expected.applicationVersion = "staging-test";
    expected.state.theme = Theme::dark;
    const auto encoded = SettingsTransferCodec::encode(expected);
    REQUIRE(encoded.document.has_value());
    const auto validFile = temporary.path.getChildFile("valid.ptsettings");
    REQUIRE(validFile.replaceWithText(*encoded.document));
    const auto valid = SettingsImportTransaction::stage(validFile, {{0, 0, 1920, 1080}});
    REQUIRE(valid.status == SettingsImportStageStatus::ready);
    REQUIRE(valid.model.has_value());
    CHECK(SettingsTransferCodec::encode(*valid.model).document == encoded.document);
}

TEST_CASE(
    "settings import transaction completely replaces live and persisted transfer state",
    "[settings][transfer][transaction]")
{
    SettingsTransferModel live;
    live.applicationVersion = "prior";
    live.state.theme = Theme::light;
    live.state.workspaceCatalog.named.push_back(
        {"prior", "Prior Workspace", WorkspaceBuiltIns::pitchPractice().snapshot});
    auto persisted = live;
    std::string privateNativeProperty = "preserved feedback draft";

    SettingsTransferModel candidate;
    candidate.applicationVersion = "imported";
    candidate.feedbackInvitationsDisabled = true;
    candidate.state.theme = Theme::dark;
    candidate.state.microphoneMuted = true;
    candidate.state.workspaceCatalog.named.push_back(
        {"imported", "Imported Workspace", WorkspaceBuiltIns::spectrumAnalysis().snapshot});

    bool automaticSaveEnabled = true;
    const auto result = SettingsImportTransaction::execute(
        candidate, true, automaticSaveEnabled,
        {[&] { return live; },
         [&](const SettingsTransferModel& state)
         {
             CHECK_FALSE(automaticSaveEnabled);
             live = state;
             return true;
         },
         [&](const SettingsTransferModel& state)
         {
             CHECK_FALSE(automaticSaveEnabled);
             persisted = state;
             return true;
         }});

    CHECK(result.status == SettingsImportStatus::imported);
    CHECK(
        SettingsTransferCodec::encode(live).document ==
        SettingsTransferCodec::encode(candidate).document);
    CHECK(
        SettingsTransferCodec::encode(persisted).document ==
        SettingsTransferCodec::encode(candidate).document);
    CHECK(privateNativeProperty == "preserved feedback draft");
    CHECK(automaticSaveEnabled);
}

TEST_CASE(
    "settings import transaction rolls back failed live apply without persistence",
    "[settings][transfer][transaction]")
{
    SettingsTransferModel prior;
    prior.applicationVersion = "prior";
    SettingsTransferModel live = prior;
    const auto diskBefore = std::string{"prior settings file"};
    auto diskAfter = diskBefore;
    int applyCount = 0;
    int persistCount = 0;

    SettingsTransferModel candidate;
    candidate.applicationVersion = "candidate";
    bool automaticSaveEnabled = true;
    const auto result = SettingsImportTransaction::execute(
        candidate, true, automaticSaveEnabled,
        {[&] { return live; },
         [&](const SettingsTransferModel& state)
         {
             live = state;
             ++applyCount;
             return applyCount != 1;
         },
         [&](const SettingsTransferModel&)
         {
             ++persistCount;
             diskAfter = "changed";
             return true;
         }});

    CHECK(result.status == SettingsImportStatus::applyFailed);
    CHECK(
        SettingsTransferCodec::encode(live).document ==
        SettingsTransferCodec::encode(prior).document);
    CHECK(applyCount == 2);
    CHECK(persistCount == 0);
    CHECK(diskAfter == diskBefore);
}

TEST_CASE(
    "settings import transaction rolls back failed persistence and reports rollback failure",
    "[settings][transfer][transaction]")
{
    SettingsTransferModel prior;
    prior.applicationVersion = "prior";
    SettingsTransferModel live = prior;
    const auto diskBefore = std::string{"prior settings file"};
    auto diskAfter = diskBefore;
    int applyCount = 0;

    SettingsTransferModel candidate;
    candidate.applicationVersion = "candidate";
    bool automaticSaveEnabled = true;
    const auto persistenceFailure = SettingsImportTransaction::execute(
        candidate, true, automaticSaveEnabled,
        {[&] { return live; },
         [&](const SettingsTransferModel& state)
         {
             live = state;
             ++applyCount;
             return true;
         },
         [&](const SettingsTransferModel&) { return false; }});

    CHECK(persistenceFailure.status == SettingsImportStatus::persistenceFailed);
    CHECK(
        SettingsTransferCodec::encode(live).document ==
        SettingsTransferCodec::encode(prior).document);
    CHECK(applyCount == 2);
    CHECK(diskAfter == diskBefore);

    applyCount = 0;
    const auto rollbackFailure = SettingsImportTransaction::execute(
        candidate, true, automaticSaveEnabled,
        {[&] { return prior; },
         [&](const SettingsTransferModel&)
         {
             ++applyCount;
             return applyCount == 1;
         },
         [&](const SettingsTransferModel&) { return false; }});

    CHECK(rollbackFailure.status == SettingsImportStatus::rollbackFailed);
    CHECK_FALSE(rollbackFailure.error.empty());
}

TEST_CASE(
    "settings import persistence atomically replaces transfer state and preserves native "
    "properties",
    "[settings][transfer][transaction][file]")
{
    TemporarySettingsFile temporary;
    const auto options = settingsOptions();
    SettingsTransferModel candidate;
    candidate.state.theme = Theme::dark;
    candidate.state.microphoneMuted = true;
    candidate.state.inputGain = 1.25;
    candidate.feedbackInvitationsDisabled = true;
    candidate.state.workspaceCatalog.named.push_back(
        {"imported", "Imported Workspace", WorkspaceBuiltIns::spectrumAnalysis().snapshot});

    {
        juce::PropertiesFile current(temporary.path, options);
        AppSettings::State prior;
        prior.theme = Theme::light;
        AppSettings::store(current, prior);
        current.setValue("feedback.draft", "private draft");
        current.setValue("installation.id", "private identity");
        REQUIRE(current.save());

        REQUIRE(SettingsImportPersistence::persist(current, options, candidate));
        CHECK(current.getValue("feedback.draft") == "private draft");
        CHECK(current.getValue("installation.id") == "private identity");
        CHECK(
            current.getBoolValue(SettingsImportPersistence::feedbackInvitationsDisabledKey, false));
    }

    juce::PropertiesFile reopened(temporary.path, options);
    REQUIRE(reopened.isValidFile());
    const auto loaded = AppSettings::load(reopened);
    CHECK(loaded.status == AppSettings::LoadStatus::loaded);
    CHECK(loaded.state.theme == Theme::dark);
    CHECK(loaded.state.microphoneMuted);
    CHECK(loaded.state.inputGain == Catch::Approx(1.25));
    REQUIRE(loaded.state.workspaceCatalog.named.size() == 1);
    CHECK(loaded.state.workspaceCatalog.named.front().name == "Imported Workspace");
    CHECK(reopened.getValue("feedback.draft") == "private draft");
    CHECK(reopened.getValue("installation.id") == "private identity");
    CHECK(reopened.getBoolValue(SettingsImportPersistence::feedbackInvitationsDisabledKey, false));
}

TEST_CASE(
    "settings transfer decoding rejects malformed markers and required fields",
    "[settings][transfer][decode]")
{
    const auto malformed = SettingsTransferCodec::decode("{not-json", {});
    CHECK(malformed.status == SettingsTransferDecodeStatus::invalid);
    CHECK_FALSE(malformed.model.has_value());
    CHECK_FALSE(malformed.error.empty());

    auto wrongMarkerDocument = parsedDocument(encodedDocument());
    wrongMarkerDocument.getDynamicObject()->setProperty("format", "other-settings");
    const auto wrongMarker =
        SettingsTransferCodec::decode(serializedDocument(wrongMarkerDocument), {});
    CHECK(wrongMarker.status == SettingsTransferDecodeStatus::invalid);
    CHECK_FALSE(wrongMarker.model.has_value());

    auto missingFieldDocument = parsedDocument(encodedDocument());
    missingFieldDocument.getDynamicObject()->removeProperty("applicationVersion");
    const auto missingField =
        SettingsTransferCodec::decode(serializedDocument(missingFieldDocument), {});
    CHECK(missingField.status == SettingsTransferDecodeStatus::invalid);
    CHECK_FALSE(missingField.model.has_value());

    auto invalidFieldDocument = parsedDocument(encodedDocument());
    auto* settings =
        invalidFieldDocument.getDynamicObject()->getProperty("settings").getDynamicObject();
    REQUIRE(settings != nullptr);
    auto* appearance = settings->getProperty("appearance").getDynamicObject();
    REQUIRE(appearance != nullptr);
    appearance->setProperty("theme", "sepia");
    const auto invalidField =
        SettingsTransferCodec::decode(serializedDocument(invalidFieldDocument), {});
    CHECK(invalidField.status == SettingsTransferDecodeStatus::invalid);
    CHECK_FALSE(invalidField.model.has_value());
}

TEST_CASE(
    "settings transfer decoding ignores additive unknown fields",
    "[settings][transfer][decode]")
{
    auto document = parsedDocument(encodedDocument());
    auto* root = document.getDynamicObject();
    REQUIRE(root != nullptr);
    root->setProperty("futureRootValue", "ignored");
    auto* settings = root->getProperty("settings").getDynamicObject();
    REQUIRE(settings != nullptr);
    settings->setProperty("futureSettingsSection", new juce::DynamicObject());

    const auto decoded = SettingsTransferCodec::decode(serializedDocument(document), {});

    REQUIRE(decoded.status == SettingsTransferDecodeStatus::loaded);
    REQUIRE(decoded.model.has_value());
    CHECK(decoded.model->applicationVersion == "decoder-test");
}

TEST_CASE(
    "settings transfer decoding migrates older schemas and rejects newer schemas",
    "[settings][transfer][decode][migration]")
{
    auto olderDocument = parsedDocument(encodedDocument());
    olderDocument.getDynamicObject()->setProperty("schemaVersion", 1);
    const auto older = SettingsTransferCodec::decode(serializedDocument(olderDocument), {});
    REQUIRE(older.status == SettingsTransferDecodeStatus::loaded);
    REQUIRE(older.model.has_value());
    CHECK(older.model->schemaVersion == SettingsTransferModel::currentSchemaVersion);

    auto newerDocument = parsedDocument(encodedDocument());
    newerDocument.getDynamicObject()->setProperty(
        "schemaVersion", SettingsTransferModel::currentSchemaVersion + 1);
    const auto newer = SettingsTransferCodec::decode(serializedDocument(newerDocument), {});
    CHECK(newer.status == SettingsTransferDecodeStatus::newerSchema);
    CHECK_FALSE(newer.model.has_value());
    CHECK_FALSE(newer.error.empty());
}

TEST_CASE(
    "settings transfer decoding enforces its document size limit",
    "[settings][transfer][decode]")
{
    const std::string oversized(SettingsTransferCodec::maximumDocumentBytes + 1, 'x');

    const auto decoded = SettingsTransferCodec::decode(oversized, {});

    CHECK(decoded.status == SettingsTransferDecodeStatus::tooLarge);
    CHECK_FALSE(decoded.model.has_value());
    CHECK_FALSE(decoded.error.empty());
}

TEST_CASE(
    "settings transfer decoding rejects conflicting workspace names",
    "[settings][transfer][decode][workspace]")
{
    SettingsTransferModel duplicateNamesModel;
    duplicateNamesModel.state.workspaceCatalog.named = {
        {"first", "Practice", WorkspaceBuiltIns::pitchPractice().snapshot},
        {"second", "practice", WorkspaceBuiltIns::spectrumAnalysis().snapshot}};
    const auto duplicateNames =
        SettingsTransferCodec::decode(encodedDocument(std::move(duplicateNamesModel)), {});
    CHECK(duplicateNames.status == SettingsTransferDecodeStatus::invalid);
    CHECK_FALSE(duplicateNames.model.has_value());

    SettingsTransferModel reservedNameModel;
    reservedNameModel.state.workspaceCatalog.named = {
        {"custom", "Pitch Practice", WorkspaceBuiltIns::spectrumAnalysis().snapshot}};
    const auto reservedName =
        SettingsTransferCodec::decode(encodedDocument(std::move(reservedNameModel)), {});
    CHECK(reservedName.status == SettingsTransferDecodeStatus::invalid);
    CHECK_FALSE(reservedName.model.has_value());
}

TEST_CASE("every setting travels in a transfer file, the devices too", "[settings][transfer]")
{
    SettingsTransferModel exported;
    auto& state = exported.state;
    state.language = "de";
    state.noteNaming = 1;
    state.octaveNumbering = 2;
    state.metronomeEnabled = false;
    state.metronomeVolume = 0.2;
    state.metronomeAccent = false;
    state.countInBars = 4;
    state.playbackEnabled = false;
    state.playbackVolume = 0.35;
    state.referencePitchHz = 442.0;
    state.nearPerfectCents = 3.0;
    state.inTuneCents = 12.0;
    state.outOfTuneCents = 30.0;
    state.minimumLevelDb = -45.0;
    state.keepTargetWhenScoreOpens = true;
    state.targetFollowsScore = false;
    state.countInWhenRecordingDuringPlayback = true;
    state.midiInputDeviceIdentifier = "usb-midi-7";
    state.midiInputDeviceName = "Casio PX-S1100";
    state.midiPedalDownThreshold = 90;
    state.audioDeviceState =
        R"(<DEVICESETUP deviceType="ALSA" audioInputDeviceName="Mic" audioOutputDeviceName="Speakers"/>)";

    const auto decoded =
        SettingsTransferCodec::decode(encodedDocument(exported), {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());
    const auto& imported = decoded.model->state;
    CHECK(decoded.model->carriesAllSettings);
    CHECK(imported.language == "de");
    CHECK(imported.noteNaming == 1);
    CHECK(imported.octaveNumbering == 2);
    CHECK_FALSE(imported.metronomeEnabled);
    CHECK(imported.metronomeVolume == Catch::Approx(0.2));
    CHECK_FALSE(imported.metronomeAccent);
    CHECK(imported.countInBars == 4);
    CHECK_FALSE(imported.playbackEnabled);
    CHECK(imported.playbackVolume == Catch::Approx(0.35));
    CHECK(imported.referencePitchHz == Catch::Approx(442.0));
    CHECK(imported.nearPerfectCents == Catch::Approx(3.0));
    CHECK(imported.inTuneCents == Catch::Approx(12.0));
    CHECK(imported.outOfTuneCents == Catch::Approx(30.0));
    CHECK(imported.minimumLevelDb == Catch::Approx(-45.0));
    CHECK(imported.keepTargetWhenScoreOpens);
    CHECK_FALSE(imported.targetFollowsScore);
    CHECK(imported.countInWhenRecordingDuringPlayback);
    CHECK(imported.midiInputDeviceIdentifier == "usb-midi-7");
    CHECK(imported.midiInputDeviceName == "Casio PX-S1100");
    CHECK(imported.midiPedalDownThreshold == 90);
    CHECK(imported.audioDeviceState.contains("Speakers"));
    CHECK(imported.audioDeviceState.contains("Mic"));
}

TEST_CASE(
    "imported settings are read into range, and a malformed language is Automatic",
    "[settings][transfer]")
{
    auto parsed = parsedDocument(encodedDocument());
    auto* settings = parsed.getProperty("settings", {}).getDynamicObject();
    REQUIRE(settings != nullptr);
    auto* appearance = settings->getProperty("appearance").getDynamicObject();
    auto* practice = settings->getProperty("practice").getDynamicObject();
    auto* metronome = settings->getProperty("metronome").getDynamicObject();
    auto* midi = settings->getProperty("midi").getDynamicObject();
    REQUIRE(appearance != nullptr);
    REQUIRE(practice != nullptr);
    REQUIRE(metronome != nullptr);
    REQUIRE(midi != nullptr);
    appearance->setProperty("language", "../../etc");
    appearance->setProperty("noteNaming", 9);
    practice->setProperty("referencePitchHz", 1000.0);
    practice->setProperty("minimumLevelDb", "quiet");
    metronome->setProperty("countInBars", -3);
    midi->setProperty("pedalDownThreshold", 400);

    const auto decoded =
        SettingsTransferCodec::decode(serializedDocument(parsed), {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());
    const auto& imported = decoded.model->state;
    CHECK(imported.language.isEmpty());
    CHECK(imported.noteNaming == 2);
    CHECK(imported.referencePitchHz == Catch::Approx(466.0));
    CHECK(imported.minimumLevelDb == Catch::Approx(AppDefaults::minimumLevelDb));
    CHECK(imported.countInBars == 0);
    CHECK(imported.midiPedalDownThreshold == 127);
}

TEST_CASE(
    "a bundle from before every setting travelled keeps this machine's values",
    "[settings][transfer]")
{
    SettingsTransferModel exported;
    exported.state.audioDeviceState =
        R"(<DEVICESETUP deviceType="ALSA" audioInputDeviceName="Mic"/>)";
    auto parsed = parsedDocument(encodedDocument(exported));
    auto* settings = parsed.getProperty("settings", {}).getDynamicObject();
    REQUIRE(settings != nullptr);
    for (const auto* name : {"metronome", "playback", "practice", "midi"})
    {
        REQUIRE(settings->hasProperty(name));
        settings->removeProperty(name);
    }
    auto* appearance = settings->getProperty("appearance").getDynamicObject();
    REQUIRE(appearance != nullptr);
    for (const auto* name : {"language", "noteNaming", "octaveNumbering"})
    {
        appearance->removeProperty(name);
    }

    const auto decoded =
        SettingsTransferCodec::decode(serializedDocument(parsed), {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());
    CHECK_FALSE(decoded.model->carriesAllSettings);

    AppSettings::State local;
    local.language = "fr";
    local.noteNaming = 2;
    local.metronomeVolume = 0.1;
    local.referencePitchHz = 440.0 + 3.0;
    local.midiInputDeviceIdentifier = "here";
    local.midiPedalDownThreshold = 20;
    local.chosenTargetSounding = "Bb3";
    local.audioDeviceState =
        R"(<DEVICESETUP deviceType="ALSA" audioInputDeviceName="Other" audioOutputDeviceName="Headphones"/>)";

    const auto merged = SettingsTransferCodec::withLocalPreferences(*decoded.model, local);
    CHECK(merged.carriesAllSettings);
    CHECK(merged.state.language == "fr");
    CHECK(merged.state.noteNaming == 2);
    CHECK(merged.state.metronomeVolume == Catch::Approx(0.1));
    CHECK(merged.state.referencePitchHz == Catch::Approx(443.0));
    CHECK(merged.state.midiInputDeviceIdentifier == "here");
    CHECK(merged.state.midiPedalDownThreshold == 20);
    CHECK(merged.state.chosenTargetSounding == "Bb3");
    // The imported input, and this machine's output, which the old bundle
    // never named.
    CHECK(merged.state.audioDeviceState.contains("Mic"));
    CHECK(merged.state.audioDeviceState.contains("Headphones"));
    CHECK_FALSE(merged.state.audioDeviceState.contains("Other"));
}

TEST_CASE(
    "an up-to-date bundle replaces every setting but the chosen target note",
    "[settings][transfer]")
{
    SettingsTransferModel exported;
    exported.state.language = "de";
    exported.state.midiPedalDownThreshold = 90;
    const auto decoded =
        SettingsTransferCodec::decode(encodedDocument(exported), {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());

    AppSettings::State local;
    local.language = "fr";
    local.midiPedalDownThreshold = 20;
    local.chosenTargetSounding = "Bb3";
    local.chosenTargetWritten = "C4";
    const auto merged = SettingsTransferCodec::withLocalPreferences(*decoded.model, local);
    CHECK(merged.state.language == "de");
    CHECK(merged.state.midiPedalDownThreshold == 90);
    CHECK(merged.state.chosenTargetSounding == "Bb3");
    CHECK(merged.state.chosenTargetWritten == "C4");
}

TEST_CASE(
    "the text size travels with appearance, and a bundle without it imports at 100",
    "[settings][transfer][text-size]")
{
    SettingsTransferModel exported;
    exported.state.textScalePercent = 130;
    const std::string document = encodedDocument(exported);

    auto decoded = SettingsTransferCodec::decode(document, {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());
    CHECK(decoded.model->state.textScalePercent == 130);

    // A bundle from 0.7.0, before the setting: no textScale under appearance.
    auto parsed = parsedDocument(document);
    auto* appearance =
        parsed.getProperty("settings", {}).getProperty("appearance", {}).getDynamicObject();
    REQUIRE(appearance != nullptr);
    REQUIRE(appearance->hasProperty("textScale"));
    appearance->removeProperty("textScale");
    decoded = SettingsTransferCodec::decode(serializedDocument(parsed), {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());
    CHECK(decoded.model->state.textScalePercent == 100);

    // Out of range or not a number: the nearest size offered, or 100.
    appearance->setProperty("textScale", 400);
    decoded = SettingsTransferCodec::decode(serializedDocument(parsed), {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());
    CHECK(decoded.model->state.textScalePercent == 150);
    appearance->setProperty("textScale", "large");
    decoded = SettingsTransferCodec::decode(serializedDocument(parsed), {{0, 0, 2560, 1440}});
    REQUIRE(decoded.model.has_value());
    CHECK(decoded.model->state.textScalePercent == 100);
}
