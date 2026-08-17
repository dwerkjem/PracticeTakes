#include "../../../MainComponent.h"

#include "../../../../tools/ToolInstanceAllocator.h"
#include "../../../../tools/ToolServices.h"
#include "../components/DockedToolPanel.h"
#include "../components/ToolWindow.h"

#include <utility>

MainComponent::LiveTool* MainComponent::findLiveTool(const ToolInstanceId& instance)
{
    for (auto& entry : liveTools)
    {
        if (entry.id == instance)
        {
            return &entry;
        }
    }
    return nullptr;
}

const MainComponent::LiveTool* MainComponent::findLiveTool(const ToolInstanceId& instance) const
{
    for (const auto& entry : liveTools)
    {
        if (entry.id == instance)
        {
            return &entry;
        }
    }
    return nullptr;
}

MainComponent::LiveTool* MainComponent::findLiveTool(WorkspaceLayoutState::Tool handle)
{
    for (auto& entry : liveTools)
    {
        if (entry.handle == handle)
        {
            return &entry;
        }
    }
    return nullptr;
}

const ToolDefinition* MainComponent::definitionFor(const ToolInstanceId& instance) const
{
    return builtInToolRegistry().catalog().findForInstance(instance);
}

MainComponent::LiveTool* MainComponent::ensureLiveTool(const ToolInstanceId& instance)
{
    if (auto* existing = findLiveTool(instance))
    {
        return existing;
    }
    if (definitionFor(instance) == nullptr)
    {
        return nullptr;
    }

    LiveTool entry;
    entry.id = instance;
    entry.handle = static_cast<WorkspaceLayoutState::Tool>(nextToolHandle++);
    liveTools.push_back(std::move(entry));
    return &liveTools.back();
}

ToolInstanceId MainComponent::instanceIdToOpen(std::string_view toolId) const
{
    // The policy itself lives in ToolInstanceAllocator so it can be tested
    // without a display; all the shell contributes is what is currently live.
    const auto isLive = [this](const ToolInstanceId& candidate)
    {
        const auto* existing = findLiveTool(candidate);
        return existing != nullptr && existing->state.isOpen();
    };
    return ToolInstanceAllocator::nextInstanceId(builtInToolRegistry().catalog(), toolId, isLive)
        .value_or(ToolInstanceId{});
}

void MainComponent::openToolById(
    std::string_view toolId,
    WorkspaceToolState::Presentation presentation)
{
    const auto instance = instanceIdToOpen(toolId);
    if (instance.empty())
    {
        return;
    }
    openTool(instance, presentation);
}

void MainComponent::openTool(
    const ToolInstanceId& instance,
    WorkspaceToolState::Presentation presentation)
{
    if (findLiveTool(instance) == nullptr && definitionFor(instance) == nullptr)
    {
        return;
    }

    currentTool = instance;
    if (toolIsOpen(instance))
    {
        focusTool(instance);
        return;
    }
    presentTool(instance, presentation);
}

void MainComponent::presentTool(
    const ToolInstanceId& instance,
    WorkspaceToolState::Presentation presentation)
{
    if (presentation == WorkspaceToolState::Presentation::closed)
    {
        closeTool(instance);
        return;
    }

    auto* entry = ensureLiveTool(instance);
    if (entry == nullptr)
    {
        return;
    }

    currentTool = instance;
    if (entry->component == nullptr)
    {
        static_cast<void>(entry->state.present(presentation));

        const ToolServices services{audioInputService, currentTheme};
        entry->component = builtInToolRegistry().create(instance.toolId(), services);
        if (entry->component == nullptr)
        {
            return;
        }
        if (entry->savedSettings.has_value())
        {
            entry->component->applySettings(*entry->savedSettings);
        }
    }
    else if (entry->state.presentation() == presentation)
    {
        focusTool(instance);
        return;
    }

    detachToolPresentation(instance);
    static_cast<void>(entry->state.present(presentation));

    // Keep the tiling tree in sync: docking places the tool somewhere in the
    // tree (auto-tabbing alongside whatever's already there, or becoming the
    // tree's sole pane if it's currently empty); floating removes it, since
    // only currently-docked tools are ever tracked by the tree. Drag-driven
    // docking calls insert() again right after this with a specific
    // pane/zone, which safely relocates the tool since insert() has move
    // (remove-then-place) semantics.
    if (presentation == WorkspaceToolState::Presentation::docked)
    {
        workspaceLayoutState.insert(
            entry->handle, std::nullopt, WorkspaceLayoutState::DropZone::centre);
    }
    else
    {
        workspaceLayoutState.remove(entry->handle);
    }

    constructToolPresentation(instance, presentation);
    rebuildWorkspaceContainer();
    applyAppearanceToTool(instance);
    focusTool(instance);
}

void MainComponent::constructToolPresentation(
    const ToolInstanceId& instance,
    WorkspaceToolState::Presentation presentation)
{
    jassert(presentation != WorkspaceToolState::Presentation::closed);
    auto* entry = findLiveTool(instance);
    if (entry == nullptr || entry->component == nullptr)
    {
        jassertfalse;
        return;
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    const auto closeHandler = [safeThis, instance]
    {
        if (safeThis != nullptr)
        {
            safeThis->closeTool(instance);
        }
    };
    const auto dragHandler = [safeThis, instance](juce::Component& source)
    {
        if (safeThis != nullptr)
        {
            safeThis->beginToolDrag(instance, source);
        }
    };
    const auto feedbackHandler = [safeThis, instance]
    {
        if (safeThis != nullptr)
        {
            safeThis->showFeedback(safeThis->toolName(instance));
        }
    };
    const auto focusHandler = [safeThis, instance]
    {
        if (safeThis != nullptr)
        {
            safeThis->recordToolFocus(instance);
        }
    };

    if (presentation == WorkspaceToolState::Presentation::docked)
    {
        const auto floatHandler = [safeThis, instance]
        {
            if (safeThis != nullptr)
            {
                safeThis->presentTool(instance, WorkspaceToolState::Presentation::floating);
            }
        };
        entry->dock = std::make_unique<DockedToolPanel>(
            toolName(instance), *entry->component, dragHandler, floatHandler, feedbackHandler,
            closeHandler, focusHandler, entry->component.get());
    }
    else
    {
        const auto dockHandler = [safeThis, instance]
        {
            if (safeThis != nullptr)
            {
                safeThis->presentTool(instance, WorkspaceToolState::Presentation::docked);
            }
        };
        const auto* definition = definitionFor(instance);
        const auto preferredSize =
            definition != nullptr
                ? juce::Point<
                      int>{definition->preferredSize.width, definition->preferredSize.height}
                : juce::Point<int>{900, 700};
        entry->window = std::make_unique<ToolWindow>(
            toolName(instance), *entry->component, preferredSize, dragHandler, dockHandler,
            feedbackHandler, closeHandler, focusHandler, entry->component.get());
        if (!entry->savedBounds.isEmpty())
        {
            entry->window->setBounds(entry->savedBounds);
        }
    }
}

void MainComponent::focusTool(const ToolInstanceId& instance)
{
    if (auto* entry = findLiveTool(instance))
    {
        if (entry->window != nullptr)
        {
            entry->window->setVisible(true);
            entry->window->toFront(true);
        }
        else if (entry->dock != nullptr)
        {
            entry->dock->grabKeyboardFocus();
        }
    }
    recordToolFocus(instance);
}

void MainComponent::recordToolFocus(const ToolInstanceId& instance)
{
    currentTool = instance;
    captureActiveWorkspace();
}

void MainComponent::closeTool(const ToolInstanceId& instance)
{
    auto* entry = findLiveTool(instance);
    if (entry == nullptr || !entry->state.isOpen())
    {
        return;
    }

    // Capture before destruction: once the component is gone its settings are
    // unrecoverable, and reopening should bring the tool back as it was.
    if (entry->component != nullptr)
    {
        entry->savedSettings = entry->component->captureSettings();
    }

    detachToolPresentation(instance);
    workspaceLayoutState.remove(entry->handle);
    entry->component.reset();
    static_cast<void>(entry->state.close());
    rebuildWorkspaceContainer();
    captureActiveWorkspace();
    recordSuccessfulToolUse();
}

juce::String MainComponent::toolName(const ToolInstanceId& instance) const
{
    const auto* definition = definitionFor(instance);
    if (definition == nullptr)
    {
        return {};
    }

    const auto name = juce::String(definition->displayName);
    const auto ordinal = instance.ordinal().value_or(1);

    // Only a duplicate needs disambiguating; the first instance keeps the
    // tool's plain name, which is what every existing workspace and screenshot
    // expects to see.
    return ordinal > 1 ? name + " " + juce::String(static_cast<int>(ordinal)) : name;
}

bool MainComponent::toolIsOpen(const ToolInstanceId& instance) const
{
    const auto* entry = findLiveTool(instance);
    return entry != nullptr && entry->state.isOpen();
}

void MainComponent::detachToolPresentation(const ToolInstanceId& instance)
{
    auto* entry = findLiveTool(instance);
    if (entry == nullptr)
    {
        return;
    }

    if (entry->window != nullptr)
    {
        entry->savedBounds = entry->window->getBounds();
        entry->window->releaseContent();
        entry->window.reset();
    }
    if (entry->dock != nullptr)
    {
        entry->dock->releaseContent();
        // Detach from whatever the dock's actual current parent is -- a
        // TabbedComponent needs its tab explicitly removed first (it keeps
        // its own bookkeeping of tab-index-to-content-component), while any
        // other parent (WorkspaceSplitPane, or this MainComponent directly
        // for a lone docked tool) can just have the child removed directly.
        if (auto* parent = entry->dock->getParentComponent())
        {
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(parent))
            {
                for (int index = tabs->getNumTabs(); --index >= 0;)
                {
                    if (tabs->getTabContentComponent(index) == entry->dock.get())
                    {
                        tabs->removeTab(index);
                    }
                }
            }
            else
            {
                parent->removeChildComponent(entry->dock.get());
            }
        }
        entry->dock.reset();
    }
}

void MainComponent::applyAppearanceToTool(const ToolInstanceId& instance)
{
    auto* entry = findLiveTool(instance);
    if (entry == nullptr)
    {
        return;
    }

    const auto palette = appPaletteFor(currentTheme);
    if (entry->window != nullptr)
    {
        entry->window->applyAppearance(&appLookAndFeel, palette.background);
    }
    if (entry->dock != nullptr)
    {
        entry->dock->setLookAndFeel(&appLookAndFeel);
        entry->dock->sendLookAndFeelChange();
        entry->dock->repaint();
    }

    // No dynamic_cast ladder: every tool answers setTheme through the contract,
    // so a tool the shell has never heard of is themed like any other.
    if (entry->component != nullptr)
    {
        entry->component->setTheme(currentTheme);
    }
}
