#pragma once

#include "WorkspaceBuiltIns.h"
#include "WorkspaceNormalizer.h"
#include "application/tools/BuiltInToolCatalog.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

class WorkspaceSessionLifecycle
{
  public:
    struct RestoreResult
    {
        WorkspaceCatalog catalog;
        bool recovered = false;
    };

    [[nodiscard]] static RestoreResult restore(
        WorkspaceCatalog catalog,
        const std::vector<WorkspaceBounds>& displayAreas,
        const ToolCatalog& registry = builtInToolCatalog())
    {
        bool recovered = false;

        const auto sourceActive = catalog.active;
        auto active = WorkspaceNormalizer::normalize(sourceActive, displayAreas, registry);
        recovered = active.snapshot != sourceActive || active.report.usedFallback ||
                    active.report.exceededDepthLimit || active.report.exceededCollectionLimit;
        catalog.active = std::move(active.snapshot);
        if (active.report.usedFallback)
        {
            catalog.activeSource.reset();
        }

        std::vector<NamedWorkspace> named;
        named.reserve(catalog.named.size());
        for (auto& workspace : catalog.named)
        {
            auto normalized =
                WorkspaceNormalizer::normalize(workspace.snapshot, displayAreas, registry);
            if (normalized.report.usedFallback)
            {
                recovered = true;
                continue;
            }

            recovered =
                recovered || normalized.snapshot != workspace.snapshot ||
                normalized.report.exceededDepthLimit || normalized.report.exceededCollectionLimit;
            workspace.snapshot = std::move(normalized.snapshot);
            named.push_back(std::move(workspace));
        }
        catalog.named = std::move(named);

        if (catalog.activeSource.has_value() && !sourceExists(catalog, *catalog.activeSource))
        {
            catalog.activeSource.reset();
            recovered = true;
        }

        return {std::move(catalog), recovered};
    }

    static void captureActive(WorkspaceCatalog& catalog, WorkspaceSnapshot snapshot)
    {
        catalog.active = std::move(snapshot);
    }

    static void resetActive(WorkspaceCatalog& catalog)
    {
        catalog.active = WorkspaceBuiltIns::pitchPractice().snapshot;
        catalog.activeSource = WorkspaceBuiltIns::pitchPractice().id;
    }

  private:
    [[nodiscard]] static bool sourceExists(const WorkspaceCatalog& catalog, const std::string& id)
    {
        if (WorkspaceBuiltIns::find(id) != nullptr)
        {
            return true;
        }
        return std::any_of(
            catalog.named.begin(), catalog.named.end(),
            [&id](const auto& workspace) { return workspace.id == id; });
    }
};
