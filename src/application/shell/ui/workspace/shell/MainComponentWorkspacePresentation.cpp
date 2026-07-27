#include "../../../MainComponent.h"

#include "../../../../../features/analysis/harmonics/HarmonicAnalyzerComponent.h"
#include "../../../../../features/analysis/spectrogram/SpectrogramComponent.h"
#include "../../../../../features/analysis/tuner/TunerComponent.h"
#include "../components/DockedToolPanel.h"
#include "../components/ToolWindow.h"

void MainComponent::openTool(ToolType tool, WorkspaceToolState::Presentation presentation)
{
    currentTool = tool;
    if (toolIsOpen(tool))
    {
        focusTool(tool);
        return;
    }
    presentTool(tool, presentation);
}

void MainComponent::presentTool(ToolType tool, WorkspaceToolState::Presentation presentation)
{
    if (presentation == WorkspaceToolState::Presentation::closed)
    {
        closeTool(tool);
        return;
    }

    currentTool = tool;
    auto& component = componentFor(tool);
    if (component == nullptr)
    {
        static_cast<void>(stateFor(tool).present(presentation));
        component = createToolComponent(tool);
    }
    else if (stateFor(tool).presentation() == presentation)
    {
        focusTool(tool);
        return;
    }

    detachToolPresentation(tool);
    static_cast<void>(stateFor(tool).present(presentation));

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
            static_cast<WorkspaceLayoutState::Tool>(tool), std::nullopt,
            WorkspaceLayoutState::DropZone::centre);
    }
    else
    {
        workspaceLayoutState.remove(static_cast<WorkspaceLayoutState::Tool>(tool));
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    const auto closeHandler = [safeThis, tool]
    {
        if (safeThis != nullptr)
        {
            safeThis->closeTool(tool);
        }
    };
    const auto dragHandler = [safeThis, tool](juce::Component& source)
    {
        if (safeThis != nullptr)
        {
            safeThis->beginToolDrag(tool, source);
        }
    };
    const auto feedbackHandler = [safeThis, tool]
    {
        if (safeThis != nullptr)
        {
            safeThis->showFeedback(safeThis->toolName(tool));
        }
    };
    const auto focusHandler = [safeThis, tool]
    {
        if (safeThis != nullptr)
        {
            safeThis->recordToolFocus(tool);
        }
    };

    if (presentation == WorkspaceToolState::Presentation::docked)
    {
        const auto floatHandler = [safeThis, tool]
        {
            if (safeThis != nullptr)
            {
                safeThis->presentTool(tool, WorkspaceToolState::Presentation::floating);
            }
        };
        auto& dock = dockFor(tool);
        dock = std::make_unique<DockedToolPanel>(
            toolName(tool), *component, dragHandler, floatHandler, feedbackHandler, closeHandler,
            focusHandler);
    }
    else
    {
        const auto dockHandler = [safeThis, tool]
        {
            if (safeThis != nullptr)
            {
                safeThis->presentTool(tool, WorkspaceToolState::Presentation::docked);
            }
        };
        auto& window = windowFor(tool);
        window = std::make_unique<ToolWindow>(
            toolName(tool), *component, preferredToolWindowSize(tool), dragHandler, dockHandler,
            feedbackHandler, closeHandler, focusHandler);
        const auto savedBounds =
            tool == ToolType::tuner
                ? savedTunerBounds
                : (tool == ToolType::spectrogram ? savedSpectrogramBounds : savedHarmonicBounds);
        if (!savedBounds.isEmpty())
        {
            window->setBounds(savedBounds);
        }
    }

    rebuildWorkspaceContainer();
    applyAppearanceToTool(tool);
    focusTool(tool);
}

void MainComponent::focusTool(ToolType tool)
{
    if (auto& window = windowFor(tool); window != nullptr)
    {
        window->setVisible(true);
        window->toFront(true);
    }
    else if (auto& dock = dockFor(tool); dock != nullptr)
    {
        dock->grabKeyboardFocus();
    }
    recordToolFocus(tool);
}

void MainComponent::recordToolFocus(ToolType tool)
{
    currentTool = tool;
    captureActiveWorkspace();
}

void MainComponent::closeTool(ToolType tool)
{
    if (!toolIsOpen(tool))
    {
        return;
    }

    if (auto& window = windowFor(tool); window != nullptr)
    {
        if (tool == ToolType::tuner)
        {
            savedTunerBounds = window->getBounds();
        }
        else if (tool == ToolType::spectrogram)
        {
            savedSpectrogramBounds = window->getBounds();
        }
        else
        {
            savedHarmonicBounds = window->getBounds();
        }
    }
    if (auto* tuner = dynamic_cast<TunerComponent*>(componentFor(tool).get()))
    {
        savedTunerSettings = tuner->settings();
    }

    detachToolPresentation(tool);
    workspaceLayoutState.remove(static_cast<WorkspaceLayoutState::Tool>(tool));
    componentFor(tool).reset();
    static_cast<void>(stateFor(tool).close());
    rebuildWorkspaceContainer();
    captureActiveWorkspace();
    recordSuccessfulToolUse();
}

std::unique_ptr<juce::Component> MainComponent::createToolComponent(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        auto tuner = std::make_unique<TunerComponent>(audioInputService);
        tuner->applySettings(savedTunerSettings);
        tuner->setTheme(currentTheme);
        return tuner;
    }

    if (tool == ToolType::spectrogram)
    {
        auto spectrogram = std::make_unique<SpectrogramComponent>(audioInputService);
        spectrogram->setTheme(currentTheme);
        return spectrogram;
    }

    auto harmonics = std::make_unique<HarmonicAnalyzerComponent>(audioInputService);
    harmonics->setTheme(currentTheme);
    return harmonics;
}

juce::String MainComponent::toolName(ToolType tool) const
{
    if (tool == ToolType::tuner)
    {
        return "Tuner";
    }
    return tool == ToolType::spectrogram ? "Spectrogram" : "Harmonic Analyzer";
}

juce::Point<int> MainComponent::preferredToolWindowSize(ToolType tool) const
{
    if (tool == ToolType::tuner)
    {
        return {920, 760};
    }
    return tool == ToolType::spectrogram ? juce::Point<int>{980, 650} : juce::Point<int>{980, 700};
}

bool MainComponent::toolIsOpen(ToolType tool) const
{
    return stateFor(tool).isOpen();
}

WorkspaceToolState& MainComponent::stateFor(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        return tunerState;
    }
    return tool == ToolType::spectrogram ? spectrogramState : harmonicState;
}

const WorkspaceToolState& MainComponent::stateFor(ToolType tool) const
{
    if (tool == ToolType::tuner)
    {
        return tunerState;
    }
    return tool == ToolType::spectrogram ? spectrogramState : harmonicState;
}

std::unique_ptr<juce::Component>& MainComponent::componentFor(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        return tunerComponent;
    }
    return tool == ToolType::spectrogram ? spectrogramComponent : harmonicComponent;
}

std::unique_ptr<MainComponent::ToolWindow>& MainComponent::windowFor(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        return tunerWindow;
    }
    return tool == ToolType::spectrogram ? spectrogramWindow : harmonicWindow;
}

std::unique_ptr<MainComponent::DockedToolPanel>& MainComponent::dockFor(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        return tunerDock;
    }
    return tool == ToolType::spectrogram ? spectrogramDock : harmonicDock;
}

void MainComponent::detachToolPresentation(ToolType tool)
{
    if (auto& window = windowFor(tool); window != nullptr)
    {
        if (tool == ToolType::tuner)
        {
            savedTunerBounds = window->getBounds();
        }
        else if (tool == ToolType::spectrogram)
        {
            savedSpectrogramBounds = window->getBounds();
        }
        else
        {
            savedHarmonicBounds = window->getBounds();
        }
        window->releaseContent();
        window.reset();
    }
    if (auto& dock = dockFor(tool); dock != nullptr)
    {
        dock->releaseContent();
        // Detach from whatever the dock's actual current parent is -- a
        // TabbedComponent needs its tab explicitly removed first (it keeps
        // its own bookkeeping of tab-index-to-content-component), while any
        // other parent (WorkspaceSplitPane, or this MainComponent directly
        // for a lone docked tool) can just have the child removed directly.
        if (auto* parent = dock->getParentComponent())
        {
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(parent))
            {
                for (int index = tabs->getNumTabs(); --index >= 0;)
                {
                    if (tabs->getTabContentComponent(index) == dock.get())
                    {
                        tabs->removeTab(index);
                    }
                }
            }
            else
            {
                parent->removeChildComponent(dock.get());
            }
        }
        dock.reset();
    }
}

void MainComponent::applyAppearanceToTool(ToolType tool)
{
    const auto palette = appPaletteFor(currentTheme);
    if (auto& window = windowFor(tool); window != nullptr)
    {
        window->applyAppearance(&appLookAndFeel, palette.background);
    }
    if (auto& dock = dockFor(tool); dock != nullptr)
    {
        dock->setLookAndFeel(&appLookAndFeel);
        dock->sendLookAndFeelChange();
        dock->repaint();
    }

    if (auto* tuner = dynamic_cast<TunerComponent*>(componentFor(tool).get()))
    {
        tuner->setTheme(currentTheme);
    }
    if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(componentFor(tool).get()))
    {
        spectrogram->setTheme(currentTheme);
    }
    if (auto* harmonics = dynamic_cast<HarmonicAnalyzerComponent*>(componentFor(tool).get()))
    {
        harmonics->setTheme(currentTheme);
    }
}
