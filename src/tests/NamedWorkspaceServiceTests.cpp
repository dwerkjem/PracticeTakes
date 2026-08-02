#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/NamedWorkspaceService.h"

namespace
{
WorkspaceCatalog catalogWithNamedWorkspace()
{
    WorkspaceCatalog catalog;
    catalog.active = WorkspaceBuiltIns::performancePreparation().snapshot;
    catalog.named.push_back(
        {"saved-spectrum", "Saved Spectrum", WorkspaceBuiltIns::spectrumAnalysis().snapshot});
    return catalog;
}
} // namespace

TEST_CASE("saving a named workspace trims its name and assigns a stable ID", "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    NamedWorkspaceService service([] { return std::string{"generated-id"}; });

    const auto result = service.save(catalog, std::string{"  Rehearsal  "}, false);

    CHECK(result.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(result.workspaceId == "generated-id");
    REQUIRE(catalog.named.size() == 2);
    CHECK(catalog.named.back().name == "Rehearsal");
    CHECK(catalog.named.back().snapshot == catalog.active);
    CHECK(catalog.activeSource == "generated-id");
}

TEST_CASE("saving can be cancelled without changing the catalog", "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    const auto before = catalog;
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto result = service.save(catalog, std::nullopt, false);

    CHECK(result.status == NamedWorkspaceService::ResultStatus::cancelled);
    CHECK(catalog == before);
}

TEST_CASE(
    "named workspace collisions are case insensitive and require confirmation",
    "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    const auto before = catalog;
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto pending = service.save(catalog, std::string{"saved spectrum"}, false);

    CHECK(pending.status == NamedWorkspaceService::ResultStatus::confirmationRequired);
    CHECK(pending.workspaceId == "saved-spectrum");
    CHECK(catalog == before);

    catalog.active = WorkspaceBuiltIns::vocalWarmUp().snapshot;
    const auto overwritten = service.save(catalog, std::string{"SAVED SPECTRUM"}, true);

    CHECK(overwritten.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(overwritten.workspaceId == "saved-spectrum");
    REQUIRE(catalog.named.size() == 1);
    CHECK(catalog.named.front().snapshot == catalog.active);
    CHECK(catalog.activeSource == "saved-spectrum");
}

TEST_CASE("built-in names are reserved case insensitively", "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    const auto before = catalog;
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto saveResult = service.save(catalog, std::string{" pitch PRACTICE "}, true);
    const auto renameResult =
        service.rename(catalog, "saved-spectrum", std::string{"VOCAL WARM-UP"}, true);

    CHECK(saveResult.status == NamedWorkspaceService::ResultStatus::reservedName);
    CHECK(renameResult.status == NamedWorkspaceService::ResultStatus::reservedName);
    CHECK(catalog == before);
}

TEST_CASE(
    "applying built-in and named workspaces creates independent active copies",
    "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto namedResult = service.apply(catalog, "saved-spectrum");
    REQUIRE(namedResult.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(catalog.active == catalog.named.front().snapshot);
    CHECK(catalog.activeSource == "saved-spectrum");
    catalog.active.focusedTool = "harmonic-analyzer";
    CHECK(catalog.named.front().snapshot.focusedTool == "spectrogram");

    const auto builtInResult = service.apply(catalog, WorkspaceBuiltIns::pitchPractice().id);
    REQUIRE(builtInResult.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(catalog.active == WorkspaceBuiltIns::pitchPractice().snapshot);
    CHECK(catalog.activeSource == WorkspaceBuiltIns::pitchPractice().id);
    catalog.active.focusedTool = "spectrogram";
    CHECK(WorkspaceBuiltIns::pitchPractice().snapshot.focusedTool == "tuner");
}

TEST_CASE(
    "renaming preserves the workspace ID and handles overwrite confirmation",
    "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    catalog.named.push_back({"warmup", "Warmup", WorkspaceBuiltIns::vocalWarmUp().snapshot});
    catalog.activeSource = "saved-spectrum";
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto renamed =
        service.rename(catalog, "saved-spectrum", std::string{" Analysis "}, false);
    REQUIRE(renamed.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(catalog.named.front().id == "saved-spectrum");
    CHECK(catalog.named.front().name == "Analysis");
    CHECK(catalog.activeSource == "saved-spectrum");

    const auto beforeCollision = catalog;
    const auto pending = service.rename(catalog, "saved-spectrum", std::string{"WARMUP"}, false);
    CHECK(pending.status == NamedWorkspaceService::ResultStatus::confirmationRequired);
    CHECK(pending.workspaceId == "warmup");
    CHECK(catalog == beforeCollision);

    const auto overwritten = service.rename(catalog, "saved-spectrum", std::string{"Warmup"}, true);
    CHECK(overwritten.status == NamedWorkspaceService::ResultStatus::applied);
    REQUIRE(catalog.named.size() == 1);
    CHECK(catalog.named.front().id == "saved-spectrum");
    CHECK(catalog.named.front().name == "Warmup");
}

TEST_CASE(
    "overwriting a named workspace captures active without changing its identity",
    "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    catalog.active = WorkspaceBuiltIns::vocalWarmUp().snapshot;
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto result = service.overwrite(catalog, "saved-spectrum", true);

    CHECK(result.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(catalog.named.front().id == "saved-spectrum");
    CHECK(catalog.named.front().name == "Saved Spectrum");
    CHECK(catalog.named.front().snapshot == catalog.active);
    CHECK(catalog.activeSource == "saved-spectrum");
}

TEST_CASE(
    "deleting a named active source leaves the live workspace unchanged",
    "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    catalog.activeSource = "saved-spectrum";
    const auto activeBefore = catalog.active;
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto pending = service.remove(catalog, "saved-spectrum", false);
    CHECK(pending.status == NamedWorkspaceService::ResultStatus::confirmationRequired);
    CHECK(catalog.named.size() == 1);

    const auto removed = service.remove(catalog, "saved-spectrum", true);
    CHECK(removed.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(catalog.named.empty());
    CHECK_FALSE(catalog.activeSource.has_value());
    CHECK(catalog.active == activeBefore);
}

TEST_CASE("invalid names and missing targets do not mutate the catalog", "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    const auto before = catalog;
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    CHECK(
        service.save(catalog, std::string{"   "}, false).status ==
        NamedWorkspaceService::ResultStatus::invalidName);
    CHECK(
        service.save(catalog, std::string(81, 'x'), false).status ==
        NamedWorkspaceService::ResultStatus::invalidName);
    CHECK(
        service.apply(catalog, "missing").status == NamedWorkspaceService::ResultStatus::notFound);
    CHECK(
        service.rename(catalog, "missing", std::string{"Name"}, false).status ==
        NamedWorkspaceService::ResultStatus::notFound);
    CHECK(
        service.overwrite(catalog, "missing", true).status ==
        NamedWorkspaceService::ResultStatus::notFound);
    CHECK(
        service.remove(catalog, WorkspaceBuiltIns::pitchPractice().id, true).status ==
        NamedWorkspaceService::ResultStatus::immutableBuiltIn);
    CHECK(catalog == before);
}

TEST_CASE(
    "workspace names accept eighty characters and reject catalog overflow",
    "[workspace][named]")
{
    WorkspaceCatalog catalog;
    NamedWorkspaceService service([] { return std::string{"boundary-id"}; });

    const auto boundary = service.save(catalog, std::string(80, 'x'), false);
    REQUIRE(boundary.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(catalog.named.front().name.size() == 80);

    catalog.named.clear();
    for (std::size_t index = 0; index < NamedWorkspaceService::maximumNamedWorkspaces; ++index)
    {
        catalog.named.push_back(
            {"id-" + std::to_string(index), "Name " + std::to_string(index), catalog.active});
    }
    const auto before = catalog;

    const auto overflow = service.save(catalog, std::string{"One Too Many"}, false);

    CHECK(overflow.status == NamedWorkspaceService::ResultStatus::catalogFull);
    CHECK(catalog == before);
}

TEST_CASE("rename and overwrite cancellation leave the catalog unchanged", "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    catalog.active = WorkspaceBuiltIns::vocalWarmUp().snapshot;
    const auto before = catalog;
    NamedWorkspaceService service([] { return std::string{"unused-id"}; });

    const auto renameResult = service.rename(catalog, "saved-spectrum", std::nullopt, false);
    const auto overwriteResult = service.overwrite(catalog, "saved-spectrum", false);

    CHECK(renameResult.status == NamedWorkspaceService::ResultStatus::cancelled);
    CHECK(overwriteResult.status == NamedWorkspaceService::ResultStatus::confirmationRequired);
    CHECK(catalog == before);
}

TEST_CASE("generated workspace IDs retry collisions", "[workspace][named]")
{
    auto catalog = catalogWithNamedWorkspace();
    auto attempts = 0;
    NamedWorkspaceService service(
        [&attempts]
        {
            ++attempts;
            return attempts == 1 ? std::string{"saved-spectrum"} : std::string{"unique-id"};
        });

    const auto result = service.save(catalog, std::string{"Rehearsal"}, false);

    CHECK(result.status == NamedWorkspaceService::ResultStatus::applied);
    CHECK(result.workspaceId == "unique-id");
    CHECK(attempts == 2);
}
