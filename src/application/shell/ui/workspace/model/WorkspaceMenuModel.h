#pragma once

#include "NamedWorkspaceService.h"

#include <algorithm>
#include <string>
#include <vector>

struct WorkspaceMenuEntry
{
    std::string id;
    std::string name;
    bool builtIn = false;
    bool selected = false;

    bool operator==(const WorkspaceMenuEntry&) const = default;
};

struct WorkspaceMenuModel
{
    std::vector<WorkspaceMenuEntry> entries;
    bool canSaveAs = true;
    bool canOverwrite = false;
    bool canRename = false;
    bool canDelete = false;

    [[nodiscard]] static WorkspaceMenuModel fromCatalog(const WorkspaceCatalog& catalog)
    {
        WorkspaceMenuModel model;
        model.entries.reserve(WorkspaceBuiltIns::all().size() + catalog.named.size());

        for (const auto& workspace : WorkspaceBuiltIns::all())
        {
            model.entries.push_back(
                {workspace.id, workspace.name, true, catalog.activeSource == workspace.id});
        }
        for (const auto& workspace : catalog.named)
        {
            model.entries.push_back(
                {workspace.id, workspace.name, false, catalog.activeSource == workspace.id});
        }

        model.canSaveAs = catalog.named.size() < NamedWorkspaceService::maximumNamedWorkspaces;
        const auto activeNamed =
            catalog.activeSource.has_value() &&
            std::any_of(
                catalog.named.begin(), catalog.named.end(), [&catalog](const auto& workspace)
                { return workspace.id == *catalog.activeSource; });
        model.canOverwrite = activeNamed;
        model.canRename = activeNamed;
        model.canDelete = activeNamed;
        return model;
    }
};
