#include "../../../MainComponent.h"

#include "../../../../tools/ToolServices.h"
#include "../components/ToolWindow.h"
#include "../model/WorkspaceSnapshotApply.h"
#include "../model/WorkspaceSnapshotCapture.h"

#include <utility>

namespace
{
[[nodiscard]] WorkspaceBounds workspaceBounds(const juce::Rectangle<int>& bounds)
{
    return {bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
}
} // namespace

void MainComponent::captureActiveWorkspace()
{
    std::vector<WorkspaceSnapshotCapture::ToolObservation> observations;
    observations.reserve(liveTools.size());

    for (auto& entry : liveTools)
    {
        if (entry.window != nullptr)
        {
            entry.savedBounds = entry.window->getBounds();
        }
        // Ask the tool itself rather than reaching in for a known type. A tool
        // that persists nothing returns nullopt and simply gets no entry.
        if (entry.component != nullptr)
        {
            entry.savedSettings = entry.component->captureSettings();
        }

        observations.push_back(
            {entry.id.value(), entry.handle, entry.state.presentation(),
             workspaceBounds(entry.savedBounds), entry.savedSettings});
    }

    activeWorkspaceSnapshot =
        WorkspaceSnapshotCapture::capture(workspaceLayoutState, observations, currentTool.value());
}

bool MainComponent::applyWorkspaceSnapshot(const WorkspaceSnapshot& snapshot)
{
    // Every registered tool gets an entry up front so the plan can place any of
    // them, whether or not it happens to be live right now.
    for (const auto& definition : builtInToolRegistry().tools())
    {
        static_cast<void>(ensureLiveTool(instanceIdToOpen(definition.id)));
    }

    std::vector<WorkspaceSnapshotApply::ToolBinding> bindings;
    bindings.reserve(liveTools.size());
    for (const auto& entry : liveTools)
    {
        bindings.push_back({entry.id.value(), entry.handle});
    }

    auto plan = WorkspaceSnapshotApply::plan(snapshot, builtInToolRegistry().catalog(), bindings);
    if (!plan.has_value())
    {
        return false;
    }

    return WorkspaceSnapshotApply::execute(
        std::move(*plan), automaticSettingsSaveEnabled,
        WorkspaceSnapshotApply::Callbacks{
            [this]
            {
                detachWorkspaceContainer();
                for (const auto& entry : liveTools)
                {
                    detachToolPresentation(entry.id);
                }
            },
            [this](std::unique_ptr<WorkspaceLayoutState::Node> root)
            {
                const auto replaced = workspaceLayoutState.replaceRoot(std::move(root));
                jassert(replaced);
                juce::ignoreUnused(replaced);
            },
            [this](const WorkspaceSnapshotApply::ToolDirective& directive)
            {
                auto* entry = findLiveTool(ToolInstanceId(directive.id));
                if (entry == nullptr)
                {
                    return;
                }

                // The stored payload becomes this instance's saved settings
                // whatever its presentation, so a tool restored closed still
                // reopens configured the way it was left.
                entry->savedSettings = directive.settings;

                if (directive.presentation == WorkspaceToolState::Presentation::closed)
                {
                    entry->component.reset();
                    static_cast<void>(entry->state.close());
                    return;
                }

                if (directive.presentation == WorkspaceToolState::Presentation::floating)
                {
                    entry->savedBounds = juce::Rectangle<int>{
                        directive.floatingBounds.x, directive.floatingBounds.y,
                        directive.floatingBounds.width, directive.floatingBounds.height};
                }

                if (entry->component == nullptr)
                {
                    const ToolServices services{audioInputService, currentTheme};
                    entry->component = builtInToolRegistry().create(entry->id.toolId(), services);
                }
                if (entry->component != nullptr && entry->savedSettings.has_value())
                {
                    entry->component->applySettings(*entry->savedSettings);
                }
                static_cast<void>(entry->state.present(directive.presentation));
            },
            [this](const WorkspaceSnapshotApply::ToolDirective& directive)
            {
                if (directive.presentation == WorkspaceToolState::Presentation::closed)
                {
                    return;
                }
                const auto instance = ToolInstanceId(directive.id);
                if (findLiveTool(instance) == nullptr)
                {
                    return;
                }
                constructToolPresentation(instance, directive.presentation);
                applyAppearanceToTool(instance);
            },
            [this] { rebuildWorkspaceContainer(); },
            [this](const std::optional<std::string>& focusedTool)
            {
                if (focusedTool.has_value())
                {
                    if (const auto instance = ToolInstanceId(*focusedTool);
                        findLiveTool(instance) != nullptr)
                    {
                        focusTool(instance);
                        return;
                    }
                }
                captureActiveWorkspace();
            },
        });
}
