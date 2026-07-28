#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/configuration/SettingsTransferCodec.h"
#include "application/shell/ui/workspace/model/WorkspaceBuiltIns.h"

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

    CHECK(first == second);

    const auto decoded = SettingsTransferCodec::decode(first, {{0, 0, 2560, 1440}});

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
