#include "WorkspaceCatalogCodec.h"

#include "WorkspaceBuiltIns.h"
#include "WorkspaceNormalizer.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>

namespace
{
constexpr std::size_t maximumIdentifierCharacters = 128;
constexpr std::size_t maximumPayloadCharacters = 256 * 1024;
constexpr std::size_t maximumParsedCollectionEntries = 4096;
constexpr std::size_t maximumParsedNodes = 4096;

struct SnapshotDecodeContext
{
    std::size_t nodeCount = 0;
};

[[nodiscard]] juce::String toJuceString(std::string_view value)
{
    return juce::String::fromUTF8(value.data(), static_cast<int>(value.size()));
}

[[nodiscard]] bool hasProperty(const juce::DynamicObject& object, const char* name)
{
    return object.hasProperty(juce::Identifier(name));
}

[[nodiscard]] const juce::var& property(const juce::DynamicObject& object, const char* name)
{
    return object.getProperty(juce::Identifier(name));
}

[[nodiscard]] bool readString(
    const juce::var& value,
    std::string& result,
    std::size_t maximumCharacters,
    bool allowEmpty = false)
{
    if (!value.isString())
    {
        return false;
    }

    const auto text = value.toString();
    if ((!allowEmpty && text.isEmpty()) ||
        static_cast<std::size_t>(text.length()) > maximumCharacters)
    {
        return false;
    }

    result = text.toStdString();
    return true;
}

[[nodiscard]] bool readInteger(const juce::var& value, int& result)
{
    if (value.isInt())
    {
        result = static_cast<int>(value);
        return true;
    }

    if (!value.isInt64())
    {
        return false;
    }

    const auto integer = static_cast<juce::int64>(value);
    if (integer < std::numeric_limits<int>::min() || integer > std::numeric_limits<int>::max())
    {
        return false;
    }

    result = static_cast<int>(integer);
    return true;
}

[[nodiscard]] bool readNumber(const juce::var& value, double& result)
{
    if (!value.isInt() && !value.isInt64() && !value.isDouble())
    {
        return false;
    }

    result = static_cast<double>(value);
    return std::isfinite(result);
}

[[nodiscard]] juce::var encodeBounds(const WorkspaceBounds& bounds)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("x", bounds.x);
    object->setProperty("y", bounds.y);
    object->setProperty("width", bounds.width);
    object->setProperty("height", bounds.height);
    return object.release();
}

[[nodiscard]] juce::var encodeNode(const WorkspaceNode& node)
{
    auto object = std::make_unique<juce::DynamicObject>();
    if (node.kind == WorkspaceNodeKind::leaf)
    {
        object->setProperty("kind", "leaf");
        object->setProperty("toolId", toJuceString(node.toolId));
    }
    else if (node.kind == WorkspaceNodeKind::tabs)
    {
        object->setProperty("kind", "tabs");
        juce::Array<juce::var> tabs;
        for (const auto& toolId : node.tabs)
        {
            tabs.add(toJuceString(toolId));
        }
        object->setProperty("tabs", std::move(tabs));
        object->setProperty("activeTab", toJuceString(node.activeTab));
    }
    else
    {
        object->setProperty("kind", "split");
        object->setProperty(
            "orientation",
            node.orientation == WorkspaceOrientation::horizontal ? "horizontal" : "vertical");
        object->setProperty("ratio", node.splitRatio);
        object->setProperty("first", node.first != nullptr ? encodeNode(*node.first) : juce::var());
        object->setProperty(
            "second", node.second != nullptr ? encodeNode(*node.second) : juce::var());
    }
    return object.release();
}

[[nodiscard]] juce::var encodeSnapshot(const WorkspaceSnapshot& snapshot)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty(
        "dockRoot", snapshot.dockRoot.has_value() ? encodeNode(*snapshot.dockRoot) : juce::var());

    juce::Array<juce::var> floating;
    for (const auto& entry : snapshot.floating)
    {
        auto value = std::make_unique<juce::DynamicObject>();
        value->setProperty("toolId", toJuceString(entry.toolId));
        value->setProperty("bounds", encodeBounds(entry.bounds));
        floating.add(value.release());
    }
    object->setProperty("floating", std::move(floating));
    object->setProperty(
        "focusedTool", snapshot.focusedTool.has_value()
                           ? juce::var(toJuceString(*snapshot.focusedTool))
                           : juce::var());

    auto settings = std::make_unique<juce::DynamicObject>();
    for (const auto& [toolId, payload] : snapshot.toolSettings)
    {
        const auto propertyName = toJuceString(toolId);
        if (!juce::Identifier::isValidIdentifier(propertyName))
        {
            continue;
        }

        auto value = std::make_unique<juce::DynamicObject>();
        value->setProperty("version", payload.version);
        value->setProperty("data", toJuceString(payload.data));
        settings->setProperty(juce::Identifier(propertyName), value.release());
    }
    object->setProperty("toolSettings", settings.release());
    return object.release();
}

[[nodiscard]] bool decodeBounds(const juce::var& value, WorkspaceBounds& bounds)
{
    const auto* object = value.getDynamicObject();
    return object != nullptr && hasProperty(*object, "x") && hasProperty(*object, "y") &&
           hasProperty(*object, "width") && hasProperty(*object, "height") &&
           readInteger(property(*object, "x"), bounds.x) &&
           readInteger(property(*object, "y"), bounds.y) &&
           readInteger(property(*object, "width"), bounds.width) &&
           readInteger(property(*object, "height"), bounds.height);
}

[[nodiscard]] bool decodeNode(
    const juce::var& value,
    WorkspaceNode& node,
    std::size_t depth,
    SnapshotDecodeContext& context)
{
    if (depth > WorkspaceNormalizer::maximumDockDepth || ++context.nodeCount > maximumParsedNodes)
    {
        return false;
    }

    const auto* object = value.getDynamicObject();
    std::string kind;
    if (object == nullptr || !hasProperty(*object, "kind") ||
        !readString(property(*object, "kind"), kind, 16))
    {
        return false;
    }

    if (kind == "leaf")
    {
        std::string toolId;
        if (!hasProperty(*object, "toolId") ||
            !readString(property(*object, "toolId"), toolId, maximumIdentifierCharacters))
        {
            return false;
        }
        node = WorkspaceNode::leaf(std::move(toolId));
        return true;
    }

    if (kind == "tabs")
    {
        if (!hasProperty(*object, "tabs") || !hasProperty(*object, "activeTab"))
        {
            return false;
        }
        const auto* values = property(*object, "tabs").getArray();
        if (values == nullptr ||
            static_cast<std::size_t>(values->size()) > maximumParsedCollectionEntries)
        {
            return false;
        }

        std::vector<std::string> tabs;
        tabs.reserve(static_cast<std::size_t>(values->size()));
        for (const auto& item : *values)
        {
            std::string toolId;
            if (!readString(item, toolId, maximumIdentifierCharacters))
            {
                return false;
            }
            tabs.push_back(std::move(toolId));
        }

        std::string activeTab;
        if (!readString(
                property(*object, "activeTab"), activeTab, maximumIdentifierCharacters, true))
        {
            return false;
        }
        node = WorkspaceNode::tabGroup(std::move(tabs), std::move(activeTab));
        return true;
    }

    if (kind != "split" || !hasProperty(*object, "orientation") || !hasProperty(*object, "ratio") ||
        !hasProperty(*object, "first") || !hasProperty(*object, "second"))
    {
        return false;
    }

    std::string orientation;
    double ratio = 0.5;
    if (!readString(property(*object, "orientation"), orientation, 16) ||
        (orientation != "horizontal" && orientation != "vertical") ||
        !readNumber(property(*object, "ratio"), ratio))
    {
        return false;
    }

    WorkspaceNode first;
    WorkspaceNode second;
    if (!decodeNode(property(*object, "first"), first, depth + 1, context) ||
        !decodeNode(property(*object, "second"), second, depth + 1, context))
    {
        return false;
    }
    node = WorkspaceNode::split(
        orientation == "horizontal" ? WorkspaceOrientation::horizontal
                                    : WorkspaceOrientation::vertical,
        ratio, std::move(first), std::move(second));
    return true;
}

[[nodiscard]] bool decodeSnapshot(const juce::var& value, WorkspaceSnapshot& snapshot)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || !hasProperty(*object, "dockRoot") ||
        !hasProperty(*object, "floating") || !hasProperty(*object, "toolSettings"))
    {
        return false;
    }

    WorkspaceSnapshot decoded;
    const auto& dockRoot = property(*object, "dockRoot");
    if (!dockRoot.isVoid())
    {
        SnapshotDecodeContext context;
        WorkspaceNode node;
        if (!decodeNode(dockRoot, node, 1, context))
        {
            return false;
        }
        decoded.dockRoot = std::move(node);
    }

    const auto* floating = property(*object, "floating").getArray();
    if (floating == nullptr ||
        static_cast<std::size_t>(floating->size()) > maximumParsedCollectionEntries)
    {
        return false;
    }
    decoded.floating.reserve(static_cast<std::size_t>(floating->size()));
    for (const auto& item : *floating)
    {
        const auto* entry = item.getDynamicObject();
        FloatingWorkspaceTool tool;
        if (entry == nullptr || !hasProperty(*entry, "toolId") || !hasProperty(*entry, "bounds") ||
            !readString(property(*entry, "toolId"), tool.toolId, maximumIdentifierCharacters) ||
            !decodeBounds(property(*entry, "bounds"), tool.bounds))
        {
            return false;
        }
        decoded.floating.push_back(std::move(tool));
    }

    if (hasProperty(*object, "focusedTool") && !property(*object, "focusedTool").isVoid())
    {
        std::string focusedTool;
        if (!readString(property(*object, "focusedTool"), focusedTool, maximumIdentifierCharacters))
        {
            return false;
        }
        decoded.focusedTool = std::move(focusedTool);
    }

    const auto* settings = property(*object, "toolSettings").getDynamicObject();
    if (settings == nullptr ||
        static_cast<std::size_t>(settings->getProperties().size()) > maximumParsedCollectionEntries)
    {
        return false;
    }
    for (const auto& item : settings->getProperties())
    {
        const auto* payloadObject = item.value.getDynamicObject();
        ToolSettingsPayload payload;
        if (payloadObject == nullptr || !hasProperty(*payloadObject, "version") ||
            !hasProperty(*payloadObject, "data") ||
            !readInteger(property(*payloadObject, "version"), payload.version) ||
            payload.version <= 0 ||
            !readString(
                property(*payloadObject, "data"), payload.data, maximumPayloadCharacters, true))
        {
            return false;
        }

        const auto toolId = item.name.toString().toStdString();
        if (toolId.empty() || toolId.size() > maximumIdentifierCharacters)
        {
            return false;
        }
        decoded.toolSettings.emplace(toolId, std::move(payload));
    }

    snapshot = std::move(decoded);
    return true;
}

[[nodiscard]] bool sourceExists(const WorkspaceCatalog& catalog, const std::string& source)
{
    if (WorkspaceBuiltIns::find(source) != nullptr)
    {
        return true;
    }

    for (const auto& workspace : catalog.named)
    {
        if (workspace.id == source)
        {
            return true;
        }
    }
    return false;
}
} // namespace

std::string WorkspaceCatalogCodec::encode(const WorkspaceCatalog& catalog)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("version", catalog.version);
    root->setProperty("active", encodeSnapshot(catalog.active));
    root->setProperty(
        "activeSource", catalog.activeSource.has_value()
                            ? juce::var(toJuceString(*catalog.activeSource))
                            : juce::var());

    juce::Array<juce::var> named;
    for (const auto& workspace : catalog.named)
    {
        auto value = std::make_unique<juce::DynamicObject>();
        value->setProperty("id", toJuceString(workspace.id));
        value->setProperty("name", toJuceString(workspace.name));
        value->setProperty("snapshot", encodeSnapshot(workspace.snapshot));
        named.add(value.release());
    }
    root->setProperty("named", std::move(named));
    return juce::JSON::toString(juce::var(root.release()), true, 17).toStdString();
}

WorkspaceCatalogDecodeResult WorkspaceCatalogCodec::decode(
    std::string_view document,
    const std::vector<WorkspaceBounds>& displayAreas,
    const WorkspaceToolRegistry& registry)
{
    if (document.size() > maximumDocumentBytes)
    {
        return {
            .status = WorkspaceCatalogDecodeStatus::tooLarge,
            .catalog = std::nullopt,
            .error = "Workspace catalog exceeds the 4 MiB limit"};
    }

    juce::var parsed;
    const auto parseResult = juce::JSON::parse(toJuceString(document), parsed);
    if (parseResult.failed())
    {
        return {
            .status = WorkspaceCatalogDecodeStatus::invalid,
            .catalog = std::nullopt,
            .error = parseResult.getErrorMessage().toStdString()};
    }

    const auto* root = parsed.getDynamicObject();
    int version = 0;
    if (root == nullptr || !hasProperty(*root, "version") ||
        !readInteger(property(*root, "version"), version) || version <= 0)
    {
        return {
            .status = WorkspaceCatalogDecodeStatus::invalid,
            .catalog = std::nullopt,
            .error = "Workspace catalog version is invalid"};
    }
    if (version > WorkspaceCatalog::currentVersion)
    {
        return {
            .status = WorkspaceCatalogDecodeStatus::newerSchema,
            .catalog = std::nullopt,
            .error = "Workspace catalog uses a newer schema"};
    }
    if (version != WorkspaceCatalog::currentVersion || !hasProperty(*root, "named"))
    {
        return {
            .status = WorkspaceCatalogDecodeStatus::invalid,
            .catalog = std::nullopt,
            .error = "Workspace catalog structure is invalid"};
    }

    const auto* namedValues = property(*root, "named").getArray();
    if (namedValues == nullptr ||
        static_cast<std::size_t>(namedValues->size()) > maximumNamedWorkspaces)
    {
        return {
            .status = WorkspaceCatalogDecodeStatus::invalid,
            .catalog = std::nullopt,
            .error = "Workspace catalog has too many named workspaces"};
    }

    WorkspaceCatalog catalog;
    catalog.version = version;
    bool recovered = false;
    bool activeUsedFallback = false;

    WorkspaceSnapshot active;
    if (!decodeSnapshot(property(*root, "active"), active))
    {
        catalog.active = WorkspaceBuiltIns::pitchPractice().snapshot;
        recovered = true;
        activeUsedFallback = true;
    }
    else
    {
        auto normalized = WorkspaceNormalizer::normalize(active, displayAreas, registry);
        recovered =
            normalized.snapshot != active || normalized.report.usedFallback ||
            normalized.report.exceededDepthLimit || normalized.report.exceededCollectionLimit;
        activeUsedFallback = normalized.report.usedFallback;
        catalog.active = std::move(normalized.snapshot);
    }

    if (hasProperty(*root, "activeSource") && !property(*root, "activeSource").isVoid())
    {
        std::string activeSource;
        if (readString(property(*root, "activeSource"), activeSource, maximumIdentifierCharacters))
        {
            catalog.activeSource = std::move(activeSource);
        }
        else
        {
            recovered = true;
        }
    }

    std::unordered_set<std::string> ids;
    for (const auto& item : *namedValues)
    {
        const auto* object = item.getDynamicObject();
        std::string id;
        std::string name;
        WorkspaceSnapshot snapshot;
        if (object == nullptr || !hasProperty(*object, "id") || !hasProperty(*object, "name") ||
            !hasProperty(*object, "snapshot") ||
            !readString(property(*object, "id"), id, maximumIdentifierCharacters) ||
            !readString(property(*object, "name"), name, maximumWorkspaceNameCharacters) ||
            !ids.insert(id).second || !decodeSnapshot(property(*object, "snapshot"), snapshot))
        {
            recovered = true;
            continue;
        }

        const auto trimmedName = juce::String::fromUTF8(name.c_str()).trim().toStdString();
        if (trimmedName.empty())
        {
            recovered = true;
            continue;
        }
        if (trimmedName != name)
        {
            name = trimmedName;
            recovered = true;
        }

        auto normalized = WorkspaceNormalizer::normalize(snapshot, displayAreas, registry);
        if (normalized.report.usedFallback)
        {
            recovered = true;
            continue;
        }
        recovered =
            recovered || normalized.snapshot != snapshot || normalized.report.exceededDepthLimit ||
            normalized.report.exceededCollectionLimit;
        catalog.named.push_back({std::move(id), std::move(name), std::move(normalized.snapshot)});
    }

    if (activeUsedFallback)
    {
        catalog.activeSource.reset();
    }
    else if (catalog.activeSource.has_value() && !sourceExists(catalog, *catalog.activeSource))
    {
        catalog.activeSource.reset();
        recovered = true;
    }

    return {
        .status = recovered ? WorkspaceCatalogDecodeStatus::loadedWithRecovery
                            : WorkspaceCatalogDecodeStatus::loaded,
        .catalog = std::move(catalog),
        .error = {}};
}