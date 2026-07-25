#include "../../MainComponent.h"

#include "../../../../features/analysis/spectrogram/SpectrogramComponent.h"
#include "../../../../features/analysis/tuner/TunerComponent.h"
#include "DockedToolPanel.h"
#include "ToolWindow.h"

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
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    const auto closeHandler = [safeThis, tool]
    {
        if (safeThis != nullptr)
            safeThis->closeTool(tool);
    };
    const auto dragHandler = [safeThis, tool](juce::Component& source)
    {
        if (safeThis != nullptr)
            safeThis->beginToolDrag(tool, source);
    };

    if (presentation == WorkspaceToolState::Presentation::docked)
    {
        const auto floatHandler = [safeThis, tool]
        {
            if (safeThis != nullptr)
                safeThis->presentTool(tool, WorkspaceToolState::Presentation::floating);
        };
        auto& dock = dockFor(tool);
        dock = std::make_unique<DockedToolPanel>(
            toolName(tool), *component, dragHandler, floatHandler, closeHandler);
    }
    else
    {
        auto& window = windowFor(tool);
        window = std::make_unique<ToolWindow>(
            toolName(tool), *component, preferredToolWindowSize(tool), dragHandler, closeHandler);
        const auto savedBounds =
            tool == ToolType::tuner ? savedTunerBounds : savedSpectrogramBounds;
        if (!savedBounds.isEmpty())
            window->setBounds(savedBounds);
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
}

void MainComponent::closeTool(ToolType tool)
{
    if (!toolIsOpen(tool))
        return;

    if (auto& window = windowFor(tool); window != nullptr)
    {
        if (tool == ToolType::tuner)
            savedTunerBounds = window->getBounds();
        else
            savedSpectrogramBounds = window->getBounds();
    }
    if (auto* tuner = dynamic_cast<TunerComponent*>(componentFor(tool).get()))
        savedTunerSettings = tuner->settings();

    detachToolPresentation(tool);
    componentFor(tool).reset();
    static_cast<void>(stateFor(tool).close());
    rebuildWorkspaceContainer();
    recordSuccessfulToolUse();
}

std::unique_ptr<juce::Component> MainComponent::createToolComponent(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        auto tuner = std::make_unique<TunerComponent>(
            audioInputService, [this] { showFeedback(toolName(ToolType::tuner)); });
        tuner->applySettings(savedTunerSettings);
        tuner->setTheme(currentTheme);
        return tuner;
    }

    auto spectrogram = std::make_unique<SpectrogramComponent>(
        audioInputService, [this] { showFeedback(toolName(ToolType::spectrogram)); });
    spectrogram->setTheme(currentTheme);
    return spectrogram;
}

juce::String MainComponent::toolName(ToolType tool) const
{
    return tool == ToolType::tuner ? "Tuner" : "Spectrogram";
}

juce::Point<int> MainComponent::preferredToolWindowSize(ToolType tool) const
{
    return tool == ToolType::tuner ? juce::Point<int>{920, 760} : juce::Point<int>{980, 650};
}

bool MainComponent::toolIsOpen(ToolType tool) const
{
    return stateFor(tool).isOpen();
}

WorkspaceToolState& MainComponent::stateFor(ToolType tool)
{
    return tool == ToolType::tuner ? tunerState : spectrogramState;
}

const WorkspaceToolState& MainComponent::stateFor(ToolType tool) const
{
    return tool == ToolType::tuner ? tunerState : spectrogramState;
}

std::unique_ptr<juce::Component>& MainComponent::componentFor(ToolType tool)
{
    return tool == ToolType::tuner ? tunerComponent : spectrogramComponent;
}

std::unique_ptr<MainComponent::ToolWindow>& MainComponent::windowFor(ToolType tool)
{
    return tool == ToolType::tuner ? tunerWindow : spectrogramWindow;
}

std::unique_ptr<MainComponent::DockedToolPanel>& MainComponent::dockFor(ToolType tool)
{
    return tool == ToolType::tuner ? tunerDock : spectrogramDock;
}

void MainComponent::detachToolPresentation(ToolType tool)
{
    if (auto& window = windowFor(tool); window != nullptr)
    {
        if (tool == ToolType::tuner)
            savedTunerBounds = window->getBounds();
        else
            savedSpectrogramBounds = window->getBounds();
        window->releaseContent();
        window.reset();
    }
    if (auto& dock = dockFor(tool); dock != nullptr)
    {
        if (workspaceTabs != nullptr)
        {
            for (int index = workspaceTabs->getNumTabs(); --index >= 0;)
            {
                if (workspaceTabs->getTabContentComponent(index) == dock.get())
                    workspaceTabs->removeTab(index);
            }
        }
        dock->releaseContent();
        if (dock->getParentComponent() == this)
            removeChildComponent(dock.get());
        dock.reset();
    }
}

void MainComponent::applyAppearanceToTool(ToolType tool)
{
    const auto palette = appPaletteFor(currentTheme);
    if (auto& window = windowFor(tool); window != nullptr)
        window->applyAppearance(&appLookAndFeel, palette.background);
    if (auto& dock = dockFor(tool); dock != nullptr)
    {
        dock->setLookAndFeel(&appLookAndFeel);
        dock->sendLookAndFeelChange();
        dock->repaint();
    }

    if (auto* tuner = dynamic_cast<TunerComponent*>(componentFor(tool).get()))
        tuner->setTheme(currentTheme);
    if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(componentFor(tool).get()))
        spectrogram->setTheme(currentTheme);
}
