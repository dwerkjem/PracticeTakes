#pragma once

#include "WorkspaceDocuments.h"
#include "application/tools/BuiltInToolCatalog.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class WorkspaceCatalogDecodeStatus
{
    loaded,
    loadedWithRecovery,
    invalid,
    newerSchema,
    tooLarge
};

struct WorkspaceCatalogDecodeResult
{
    WorkspaceCatalogDecodeStatus status = WorkspaceCatalogDecodeStatus::invalid;
    std::optional<WorkspaceCatalog> catalog;
    std::string error;
};

class WorkspaceCatalogCodec
{
  public:
    static constexpr std::size_t maximumDocumentBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maximumNamedWorkspaces = 128;
    static constexpr std::size_t maximumWorkspaceNameCharacters = 80;

    [[nodiscard]] static std::string encode(const WorkspaceCatalog& catalog);

    [[nodiscard]] static WorkspaceCatalogDecodeResult decode(
        std::string_view document,
        const std::vector<WorkspaceBounds>& displayAreas,
        const ToolCatalog& registry = builtInToolCatalog());
};