#include "../../MainComponent.h"

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
constexpr int horizontalLayoutMenuItemId = 31;
constexpr int verticalLayoutMenuItemId = 32;
constexpr int tabbedLayoutMenuItemId = 33;
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

    const auto canArrange = tunerState.isOpen() && spectrogramState.isOpen();
    juce::PopupMenu layoutMenu;
    layoutMenu.addItem(
        horizontalLayoutMenuItemId, "Side by side", canArrange,
        workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::horizontal);
    layoutMenu.addItem(
        verticalLayoutMenuItemId, "Stacked", canArrange,
        workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::vertical);
    layoutMenu.addItem(
        tabbedLayoutMenuItemId, "Tabbed", canArrange,
        workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::tabbed);
    menu.addSeparator();
    menu.addSubMenu("Arrange workspace", layoutMenu, canArrange);

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
            else if (selectedItemId == horizontalLayoutMenuItemId)
                safeThis->setWorkspaceLayout(WorkspaceLayoutState::Layout::horizontal);
            else if (selectedItemId == verticalLayoutMenuItemId)
                safeThis->setWorkspaceLayout(WorkspaceLayoutState::Layout::vertical);
            else if (selectedItemId == tabbedLayoutMenuItemId)
                safeThis->setWorkspaceLayout(WorkspaceLayoutState::Layout::tabbed);
        });
}
