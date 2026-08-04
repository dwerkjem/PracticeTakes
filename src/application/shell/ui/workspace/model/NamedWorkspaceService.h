#pragma once

#include "WorkspaceBuiltIns.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

class NamedWorkspaceService
{
  public:
    static constexpr std::size_t maximumNameCharacters = 80;
    static constexpr std::size_t maximumNamedWorkspaces = 128;

    enum class ResultStatus
    {
        applied,
        cancelled,
        confirmationRequired,
        invalidName,
        reservedName,
        notFound,
        immutableBuiltIn,
        catalogFull,
        idGenerationFailed
    };

    struct Result
    {
        ResultStatus status = ResultStatus::notFound;
        std::string workspaceId;
    };

    using IdGenerator = std::function<std::string()>;

    explicit NamedWorkspaceService(IdGenerator idGenerator = defaultIdGenerator())
        : generateId(std::move(idGenerator))
    {
    }

    [[nodiscard]] Result
    save(WorkspaceCatalog& catalog, const std::optional<std::string>& name, bool confirmed) const
    {
        auto prepared = prepareName(name);
        if (prepared.status != ResultStatus::applied)
        {
            return {prepared.status, std::move(prepared.workspaceId)};
        }

        const auto collision = findNamedByName(catalog, prepared.workspaceId);
        if (collision != catalog.named.end())
        {
            if (!confirmed)
            {
                return {ResultStatus::confirmationRequired, collision->id};
            }
            collision->name = prepared.workspaceId;
            collision->snapshot = catalog.active;
            catalog.activeSource = collision->id;
            return {ResultStatus::applied, collision->id};
        }

        if (catalog.named.size() >= maximumNamedWorkspaces)
        {
            return {ResultStatus::catalogFull, {}};
        }

        const auto id = uniqueId(catalog);
        if (!id.has_value())
        {
            return {ResultStatus::idGenerationFailed, {}};
        }
        catalog.named.push_back({*id, prepared.workspaceId, catalog.active});
        catalog.activeSource = *id;
        return {ResultStatus::applied, *id};
    }

    [[nodiscard]] Result apply(WorkspaceCatalog& catalog, std::string_view id) const
    {
        if (const auto* builtIn = WorkspaceBuiltIns::find(id); builtIn != nullptr)
        {
            catalog.active = builtIn->snapshot;
            catalog.activeSource = builtIn->id;
            return {ResultStatus::applied, builtIn->id};
        }

        const auto workspace = findNamedById(catalog, id);
        if (workspace == catalog.named.end())
        {
            return {ResultStatus::notFound, std::string{id}};
        }
        catalog.active = workspace->snapshot;
        catalog.activeSource = workspace->id;
        return {ResultStatus::applied, workspace->id};
    }

    [[nodiscard]] Result rename(
        WorkspaceCatalog& catalog,
        std::string_view id,
        const std::optional<std::string>& name,
        bool confirmed) const
    {
        if (WorkspaceBuiltIns::find(id) != nullptr)
        {
            return {ResultStatus::immutableBuiltIn, std::string{id}};
        }

        const auto source = findNamedById(catalog, id);
        if (source == catalog.named.end())
        {
            return {ResultStatus::notFound, std::string{id}};
        }

        auto prepared = prepareName(name);
        if (prepared.status != ResultStatus::applied)
        {
            return {prepared.status, std::move(prepared.workspaceId)};
        }

        const auto collision = findNamedByName(catalog, prepared.workspaceId);
        if (collision != catalog.named.end() && collision->id != source->id)
        {
            if (!confirmed)
            {
                return {ResultStatus::confirmationRequired, collision->id};
            }

            const auto sourceId = source->id;
            const auto sourceWasActive = catalog.activeSource == sourceId;
            const auto collisionWasActive = catalog.activeSource == collision->id;
            const auto sourceIndex = static_cast<std::size_t>(source - catalog.named.begin());
            const auto collisionIndex = static_cast<std::size_t>(collision - catalog.named.begin());
            catalog.named[sourceIndex].name = prepared.workspaceId;
            catalog.named.erase(
                catalog.named.begin() + static_cast<std::ptrdiff_t>(collisionIndex));
            if (sourceWasActive)
            {
                catalog.activeSource = sourceId;
            }
            else if (collisionWasActive)
            {
                catalog.activeSource.reset();
            }
            return {ResultStatus::applied, sourceId};
        }

        source->name = prepared.workspaceId;
        return {ResultStatus::applied, source->id};
    }

    [[nodiscard]] Result
    overwrite(WorkspaceCatalog& catalog, std::string_view id, bool confirmed) const
    {
        if (WorkspaceBuiltIns::find(id) != nullptr)
        {
            return {ResultStatus::immutableBuiltIn, std::string{id}};
        }
        const auto workspace = findNamedById(catalog, id);
        if (workspace == catalog.named.end())
        {
            return {ResultStatus::notFound, std::string{id}};
        }
        if (!confirmed)
        {
            return {ResultStatus::confirmationRequired, workspace->id};
        }

        workspace->snapshot = catalog.active;
        catalog.activeSource = workspace->id;
        return {ResultStatus::applied, workspace->id};
    }

    [[nodiscard]] Result
    remove(WorkspaceCatalog& catalog, std::string_view id, bool confirmed) const
    {
        if (WorkspaceBuiltIns::find(id) != nullptr)
        {
            return {ResultStatus::immutableBuiltIn, std::string{id}};
        }
        const auto workspace = findNamedById(catalog, id);
        if (workspace == catalog.named.end())
        {
            return {ResultStatus::notFound, std::string{id}};
        }
        if (!confirmed)
        {
            return {ResultStatus::confirmationRequired, workspace->id};
        }

        const auto removedId = workspace->id;
        catalog.named.erase(workspace);
        if (catalog.activeSource == removedId)
        {
            catalog.activeSource.reset();
        }
        return {ResultStatus::applied, removedId};
    }

  private:
    using Iterator = std::vector<NamedWorkspace>::iterator;

    IdGenerator generateId;

    [[nodiscard]] static IdGenerator defaultIdGenerator()
    {
        return []
        {
            std::array<std::uint32_t, 4> words{};
            std::random_device source;
            for (auto& word : words)
            {
                word = source();
            }

            std::ostringstream id;
            id << "workspace-" << std::hex << std::setfill('0');
            for (const auto word : words)
            {
                id << std::setw(8) << word;
            }
            return id.str();
        };
    }

    [[nodiscard]] static Result prepareName(const std::optional<std::string>& name)
    {
        if (!name.has_value())
        {
            return {ResultStatus::cancelled, {}};
        }

        auto trimmed = trim(*name);
        if (trimmed.empty() || utf8CharacterCount(trimmed) > maximumNameCharacters)
        {
            return {ResultStatus::invalidName, {}};
        }
        if (isReservedName(trimmed))
        {
            return {ResultStatus::reservedName, {}};
        }
        return {ResultStatus::applied, std::move(trimmed)};
    }

    [[nodiscard]] static std::string trim(std::string_view value)
    {
        const auto isSpace = [](const unsigned char character)
        { return std::isspace(character) != 0; };
        auto begin = value.begin();
        while (begin != value.end() && isSpace(static_cast<unsigned char>(*begin)))
        {
            ++begin;
        }
        auto end = value.end();
        while (end != begin && isSpace(static_cast<unsigned char>(*(end - 1))))
        {
            --end;
        }
        return {begin, end};
    }

    [[nodiscard]] static std::size_t utf8CharacterCount(std::string_view value)
    {
        return static_cast<std::size_t>(std::count_if(
            value.begin(), value.end(),
            [](const unsigned char byte) { return (byte & 0xc0U) != 0x80U; }));
    }

    [[nodiscard]] static std::string folded(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const auto character : value)
        {
            result.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        return result;
    }

    [[nodiscard]] static bool isReservedName(std::string_view name)
    {
        const auto key = folded(name);
        return std::any_of(
            WorkspaceBuiltIns::all().begin(), WorkspaceBuiltIns::all().end(),
            [&key](const auto& builtIn) { return folded(builtIn.name) == key; });
    }

    [[nodiscard]] static Iterator findNamedById(WorkspaceCatalog& catalog, std::string_view id)
    {
        return std::find_if(
            catalog.named.begin(), catalog.named.end(),
            [id](const auto& workspace) { return workspace.id == id; });
    }

    [[nodiscard]] static Iterator findNamedByName(WorkspaceCatalog& catalog, std::string_view name)
    {
        const auto key = folded(name);
        return std::find_if(
            catalog.named.begin(), catalog.named.end(),
            [&key](const auto& workspace) { return folded(workspace.name) == key; });
    }

    [[nodiscard]] std::optional<std::string> uniqueId(const WorkspaceCatalog& catalog) const
    {
        for (std::size_t attempt = 0;
             attempt < maximumNamedWorkspaces + WorkspaceBuiltIns::all().size(); ++attempt)
        {
            auto id = generateId();
            const auto collidesWithNamed = std::any_of(
                catalog.named.begin(), catalog.named.end(),
                [&id](const auto& workspace) { return workspace.id == id; });
            if (!id.empty() && WorkspaceBuiltIns::find(id) == nullptr && !collidesWithNamed)
            {
                return id;
            }
        }
        return std::nullopt;
    }
};
