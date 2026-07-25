#include "../MainComponent.h"

#include "../../spectrogram/SpectrogramComponent.h"
#include "../../tuner/TunerComponent.h"
#include "DockedToolPanel.h"
#include "ToolWindow.h"

namespace
{
constexpr int toolsMenuWidth = 190;
constexpr int tunerMenuItemId = 1;
constexpr int spectrogramMenuItemId = 2;
constexpr int dockTunerMenuItemId = 11;
constexpr int floatTunerMenuItemId = 12;
constexpr int closeTunerMenuItemId = 13;
constexpr int dockSpectrogramMenuItemId = 21;
constexpr int floatSpectrogramMenuItemId = 22;
constexpr int closeSpectrogramMenuItemId = 23;
} // namespace

void MainComponent::showToolsMenu()
{
    const auto tunerPresentation = tunerState.presentation();
    const auto spectrogramPresentation = spectrogramState.presentation();

    juce::PopupMenu tunerMenu;
    tunerMenu.addItem(tunerMenuItemId, "Open or focus", true, tunerState.isOpen());
    tunerMenu.addItem(
        dockTunerMenuItemId, "Dock in workspace", true,
        tunerPresentation == WorkspaceToolState::Presentation::docked);
    tunerMenu.addItem(
        floatTunerMenuItemId, "Float in window", true,
        tunerPresentation == WorkspaceToolState::Presentation::floating);
    tunerMenu.addSeparator();
    tunerMenu.addItem(closeTunerMenuItemId, "Close", tunerState.isOpen());

    juce::PopupMenu spectrogramMenu;
    spectrogramMenu.addItem(
        spectrogramMenuItemId, "Open or focus", true, spectrogramState.isOpen());
    spectrogramMenu.addItem(
        dockSpectrogramMenuItemId, "Dock in workspace", true,
        spectrogramPresentation == WorkspaceToolState::Presentation::docked);
    spectrogramMenu.addItem(
        floatSpectrogramMenuItemId, "Float in window", true,
        spectrogramPresentation == WorkspaceToolState::Presentation::floating);
    spectrogramMenu.addSeparator();
    spectrogramMenu.addItem(closeSpectrogramMenuItemId, "Close", spectrogramState.isOpen());

    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addSubMenu("Tuner", tunerMenu);
    menu.addSubMenu("Spectrogram", spectrogramMenu);

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetComponent(&toolsButton)
            .withMinimumWidth(toolsMenuWidth),
        [safeThis](int selectedItemId)
        {
            if (safeThis == nullptr)
                return;

            if (selectedItemId == tunerMenuItemId)
                safeThis->openTool(ToolType::tuner);
            else if (selectedItemId == spectrogramMenuItemId)
                safeThis->openTool(ToolType::spectrogram);
            else if (selectedItemId == dockTunerMenuItemId)
                safeThis->presentTool(ToolType::tuner, WorkspaceToolState::Presentation::docked);
            else if (selectedItemId == floatTunerMenuItemId)
                safeThis->presentTool(ToolType::tuner, WorkspaceToolState::Presentation::floating);
            else if (selectedItemId == closeTunerMenuItemId)
                safeThis->closeTool(ToolType::tuner);
            else if (selectedItemId == dockSpectrogramMenuItemId)
                safeThis->presentTool(
                    ToolType::spectrogram, WorkspaceToolState::Presentation::docked);
            else if (selectedItemId == floatSpectrogramMenuItemId)
                safeThis->presentTool(
                    ToolType::spectrogram, WorkspaceToolState::Presentation::floating);
            else if (selectedItemId == closeSpectrogramMenuItemId)
                safeThis->closeTool(ToolType::spectrogram);
        });
}

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

    if (presentation == WorkspaceToolState::Presentation::docked)
    {
        const auto floatHandler = [safeThis, tool]
        {
            if (safeThis != nullptr)
                safeThis->presentTool(tool, WorkspaceToolState::Presentation::floating);
        };
        auto& dock = dockFor(tool);
        dock = std::make_unique<DockedToolPanel>(
            toolName(tool), *component, floatHandler, closeHandler);
        addAndMakeVisible(*dock);
        resized();
    }
    else
    {
        auto& window = windowFor(tool);
        window = std::make_unique<ToolWindow>(
            toolName(tool), *component, preferredToolWindowSize(tool), closeHandler);
        const auto savedBounds =
            tool == ToolType::tuner ? savedTunerBounds : savedSpectrogramBounds;
        if (!savedBounds.isEmpty())
            window->setBounds(savedBounds);
    }

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
    resized();
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
        dock->releaseContent();
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
