#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceMenuModel.h"

TEST_CASE(
    "workspace menu lists built-ins and named workspaces with active selection",
    "[workspace][menu]")
{
    WorkspaceCatalog catalog;
    catalog.named.push_back(
        {"saved-spectrum", "Saved Spectrum", WorkspaceBuiltIns::spectrumAnalysis().snapshot});
    catalog.activeSource = "saved-spectrum";

    const auto model = WorkspaceMenuModel::fromCatalog(catalog);

    REQUIRE(model.entries.size() == 5);
    CHECK(model.entries[0] == WorkspaceMenuEntry{"pitch-practice", "Pitch Practice", true, false});
    CHECK(model.entries[4] == WorkspaceMenuEntry{"saved-spectrum", "Saved Spectrum", false, true});
    CHECK(model.canSaveAs);
    CHECK(model.canOverwrite);
    CHECK(model.canRename);
    CHECK(model.canDelete);
}

TEST_CASE(
    "workspace management disables named-only actions for built-ins and edited copies",
    "[workspace][menu]")
{
    WorkspaceCatalog catalog;
    catalog.named.push_back(
        {"saved-spectrum", "Saved Spectrum", WorkspaceBuiltIns::spectrumAnalysis().snapshot});

    catalog.activeSource = WorkspaceBuiltIns::pitchPractice().id;
    const auto builtIn = WorkspaceMenuModel::fromCatalog(catalog);
    REQUIRE_FALSE(builtIn.entries.empty());
    CHECK(builtIn.entries.front().selected);
    CHECK_FALSE(builtIn.canOverwrite);
    CHECK_FALSE(builtIn.canRename);
    CHECK_FALSE(builtIn.canDelete);

    catalog.activeSource.reset();
    const auto edited = WorkspaceMenuModel::fromCatalog(catalog);
    CHECK_FALSE(edited.canOverwrite);
    CHECK_FALSE(edited.canRename);
    CHECK_FALSE(edited.canDelete);
}

TEST_CASE("workspace menu disables Save As at the catalog limit", "[workspace][menu]")
{
    WorkspaceCatalog catalog;
    for (std::size_t index = 0; index < NamedWorkspaceService::maximumNamedWorkspaces; ++index)
    {
        catalog.named.push_back(
            {"id-" + std::to_string(index), "Name " + std::to_string(index), catalog.active});
    }

    const auto model = WorkspaceMenuModel::fromCatalog(catalog);

    CHECK_FALSE(model.canSaveAs);
}
