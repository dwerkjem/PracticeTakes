#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceBuiltIns.h"
#include "application/shell/ui/workspace/model/WorkspaceCatalogCodec.h"

#include <string>

namespace
{
[[nodiscard]] WorkspaceCatalog sampleCatalog()
{
    WorkspaceCatalog catalog;
    catalog.active.dockRoot = WorkspaceNode::split(
        WorkspaceOrientation::horizontal, 0.61, WorkspaceNode::leaf("tuner"),
        WorkspaceNode::leaf("spectrogram"));
    catalog.active.floating = {{"harmonic-analyzer", {1200, 80, 880, 640}}};
    catalog.active.focusedTool = "spectrogram";
    catalog.active.toolSettings.emplace(
        "tuner",
        ToolSettingsPayload{
            1,
            R"({"displayMode":2,"easing":0.4,"averaging":8,"noteSwitchSemitones":0.7,"dropoutFrames":6,"graphDurationSeconds":25})"});
    catalog.activeSource = "named-study";
    catalog.named.push_back({"named-study", "Pitch Study", catalog.active});
    return catalog;
}
} // namespace

TEST_CASE(
    "workspace catalog codec has a deterministic structured round trip",
    "[workspace][catalog][codec]")
{
    auto catalog = sampleCatalog();

    const auto first = WorkspaceCatalogCodec::encode(catalog);
    const auto second = WorkspaceCatalogCodec::encode(catalog);
    const auto decoded = WorkspaceCatalogCodec::decode(first, {{0, 0, 2560, 1400}});

    CHECK(first == second);
    REQUIRE(decoded.status == WorkspaceCatalogDecodeStatus::loaded);
    REQUIRE(decoded.catalog.has_value());
    CHECK(*decoded.catalog == catalog);
}

TEST_CASE(
    "workspace catalog codec rejects malformed and unsupported documents",
    "[workspace][catalog][codec]")
{
    const auto malformed = WorkspaceCatalogCodec::decode("{not-json", {});
    CHECK(malformed.status == WorkspaceCatalogDecodeStatus::invalid);
    CHECK_FALSE(malformed.catalog.has_value());

    const auto newer = WorkspaceCatalogCodec::decode(
        R"({"version":999,"active":{"dockRoot":{"kind":"leaf","toolId":"tuner"},"floating":[],"toolSettings":{}},"named":[]})",
        {});
    CHECK(newer.status == WorkspaceCatalogDecodeStatus::newerSchema);
    CHECK_FALSE(newer.catalog.has_value());
}

TEST_CASE(
    "workspace catalog codec enforces document and named-workspace bounds",
    "[workspace][catalog][codec]")
{
    const std::string oversized(WorkspaceCatalogCodec::maximumDocumentBytes + 1, 'x');
    CHECK(
        WorkspaceCatalogCodec::decode(oversized, {}).status ==
        WorkspaceCatalogDecodeStatus::tooLarge);

    auto catalog = sampleCatalog();
    catalog.named.clear();
    for (std::size_t index = 0; index <= WorkspaceCatalogCodec::maximumNamedWorkspaces; ++index)
    {
        catalog.named.push_back(
            {"named-" + std::to_string(index), "Workspace " + std::to_string(index),
             WorkspaceBuiltIns::pitchPractice().snapshot});
    }

    const auto decoded = WorkspaceCatalogCodec::decode(WorkspaceCatalogCodec::encode(catalog), {});
    CHECK(decoded.status == WorkspaceCatalogDecodeStatus::invalid);
    CHECK_FALSE(decoded.catalog.has_value());
}

TEST_CASE(
    "workspace catalog codec omits malformed named layouts independently",
    "[workspace][catalog][codec]")
{
    const auto decoded = WorkspaceCatalogCodec::decode(
        R"({"version":1,"active":{"dockRoot":{"kind":"leaf","toolId":"tuner"},"floating":[],"toolSettings":{}},"activeSource":"broken","named":[{"id":"valid","name":"Valid","snapshot":{"dockRoot":{"kind":"leaf","toolId":"spectrogram"},"floating":[],"toolSettings":{}}},{"id":"broken","name":"Broken","snapshot":{"dockRoot":{"kind":"split","orientation":"horizontal","ratio":0.5},"floating":[],"toolSettings":{}}}]})",
        {{0, 0, 1920, 1040}});

    REQUIRE(decoded.status == WorkspaceCatalogDecodeStatus::loadedWithRecovery);
    REQUIRE(decoded.catalog.has_value());
    REQUIRE(decoded.catalog->named.size() == 1);
    CHECK(decoded.catalog->named.front().id == "valid");
    CHECK_FALSE(decoded.catalog->activeSource.has_value());
}

TEST_CASE(
    "workspace catalog codec falls back from an unrecoverable active layout",
    "[workspace][catalog][codec]")
{
    const auto unavailable = WorkspaceCatalogCodec::decode(
        R"({"version":1,"active":{"dockRoot":{"kind":"leaf","toolId":"missing-tool"},"floating":[],"toolSettings":{}},"activeSource":"valid","named":[{"id":"valid","name":"Valid","snapshot":{"dockRoot":{"kind":"leaf","toolId":"spectrogram"},"floating":[],"toolSettings":{}}}]})",
        {{0, 0, 1920, 1040}});

    REQUIRE(unavailable.status == WorkspaceCatalogDecodeStatus::loadedWithRecovery);
    REQUIRE(unavailable.catalog.has_value());
    CHECK(unavailable.catalog->active == WorkspaceBuiltIns::pitchPractice().snapshot);
    CHECK_FALSE(unavailable.catalog->activeSource.has_value());
    REQUIRE(unavailable.catalog->named.size() == 1);

    const auto malformed = WorkspaceCatalogCodec::decode(
        R"({"version":1,"active":{"dockRoot":{"kind":"split","orientation":"horizontal","ratio":0.5},"floating":[],"toolSettings":{}},"named":[{"id":"valid","name":"Valid","snapshot":{"dockRoot":{"kind":"leaf","toolId":"spectrogram"},"floating":[],"toolSettings":{}}}]})",
        {{0, 0, 1920, 1040}});

    REQUIRE(malformed.status == WorkspaceCatalogDecodeStatus::loadedWithRecovery);
    REQUIRE(malformed.catalog.has_value());
    CHECK(malformed.catalog->active == WorkspaceBuiltIns::pitchPractice().snapshot);
    REQUIRE(malformed.catalog->named.size() == 1);
}