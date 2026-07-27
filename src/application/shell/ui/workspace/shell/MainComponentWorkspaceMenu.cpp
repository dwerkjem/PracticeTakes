#include "../../../MainComponent.h"

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
constexpr int harmonicMenuItemId = 3;
constexpr int dockHarmonicMenuItemId = 41;
constexpr int floatHarmonicMenuItemId = 42;
constexpr int closeHarmonicMenuItemId = 43;
constexpr int horizontalLayoutMenuItemId = 31;
constexpr int verticalLayoutMenuItemId = 32;
constexpr int tabbedLayoutMenuItemId = 33;
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
constexpr int performanceLabMenuItemId = 50;
#endif
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

    const auto harmonicPresentation = harmonicState.presentation();
    juce::PopupMenu harmonicMenu;
    harmonicMenu.addItem(harmonicMenuItemId, "Open or focus", true, harmonicState.isOpen());
    harmonicMenu.addItem(
        dockHarmonicMenuItemId, "Dock in workspace", true,
        harmonicPresentation == WorkspaceToolState::Presentation::docked);
    harmonicMenu.addItem(
        floatHarmonicMenuItemId, "Float in window", true,
        harmonicPresentation == WorkspaceToolState::Presentation::floating);
    harmonicMenu.addSeparator();
    harmonicMenu.addItem(closeHarmonicMenuItemId, "Close", harmonicState.isOpen());
    menu.addSubMenu("Harmonic Analyzer", harmonicMenu);

    const auto dockedCount = dockedTools().size();
    const auto canArrangeSplit = workspaceLayoutState.canSplitRoot();
    juce::PopupMenu layoutMenu;
    layoutMenu.addItem(
        horizontalLayoutMenuItemId, "Side by side", canArrangeSplit,
        workspaceLayoutState.isRootOrientation(WorkspaceLayoutState::Orientation::horizontal));
    layoutMenu.addItem(
        verticalLayoutMenuItemId, "Stacked", canArrangeSplit,
        workspaceLayoutState.isRootOrientation(WorkspaceLayoutState::Orientation::vertical));
    layoutMenu.addItem(
        tabbedLayoutMenuItemId, "Tabbed", dockedCount >= 2, workspaceLayoutState.isFlattenedTabs());
    menu.addSeparator();
    menu.addSubMenu("Arrange workspace", layoutMenu, canArrangeSplit || dockedCount >= 2);
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
    menu.addSeparator();
    menu.addItem(performanceLabMenuItemId, "Performance Lab");
#endif

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetComponent(&toolsButton)
            .withMinimumWidth(toolsMenuWidth),
        [safeThis](int selectedItemId)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            if (selectedItemId == tunerMenuItemId)
            {
                safeThis->openTool(ToolType::tuner);
            }
            else if (selectedItemId == spectrogramMenuItemId)
            {
                safeThis->openTool(ToolType::spectrogram);
            }
            else if (selectedItemId == dockTunerMenuItemId)
            {
                safeThis->presentTool(ToolType::tuner, WorkspaceToolState::Presentation::docked);
            }
            else if (selectedItemId == floatTunerMenuItemId)
            {
                safeThis->presentTool(ToolType::tuner, WorkspaceToolState::Presentation::floating);
            }
            else if (selectedItemId == closeTunerMenuItemId)
            {
                safeThis->closeTool(ToolType::tuner);
            }
            else if (selectedItemId == dockSpectrogramMenuItemId)
            {
                safeThis->presentTool(
                    ToolType::spectrogram, WorkspaceToolState::Presentation::docked);
            }
            else if (selectedItemId == floatSpectrogramMenuItemId)
            {
                safeThis->presentTool(
                    ToolType::spectrogram, WorkspaceToolState::Presentation::floating);
            }
            else if (selectedItemId == closeSpectrogramMenuItemId)
            {
                safeThis->closeTool(ToolType::spectrogram);
            }
            else if (selectedItemId == harmonicMenuItemId)
            {
                safeThis->openTool(ToolType::harmonics);
            }
            else if (selectedItemId == dockHarmonicMenuItemId)
            {
                safeThis->presentTool(
                    ToolType::harmonics, WorkspaceToolState::Presentation::docked);
            }
            else if (selectedItemId == floatHarmonicMenuItemId)
            {
                safeThis->presentTool(
                    ToolType::harmonics, WorkspaceToolState::Presentation::floating);
            }
            else if (selectedItemId == closeHarmonicMenuItemId)
            {
                safeThis->closeTool(ToolType::harmonics);
            }
            else if (selectedItemId == horizontalLayoutMenuItemId)
            {
                safeThis->setWorkspaceLayout(WorkspaceLayoutState::Layout::horizontal);
            }
            else if (selectedItemId == verticalLayoutMenuItemId)
            {
                safeThis->setWorkspaceLayout(WorkspaceLayoutState::Layout::vertical);
            }
            else if (selectedItemId == tabbedLayoutMenuItemId)
            {
                safeThis->setWorkspaceLayout(WorkspaceLayoutState::Layout::tabbed);
            }
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
            else if (selectedItemId == performanceLabMenuItemId)
            {
                safeThis->showPerformanceLab();
            }
#endif
        });
}

#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
#include "../../performance/PerformanceLabWindow.h"

void MainComponent::showPerformanceLab()
{
    if (performanceLabWindow == nullptr)
    {
        performanceLabWindow =
            std::make_unique<PerformanceLabWindow>([this] { closePerformanceLab(); });
        const auto palette = appPaletteFor(currentTheme);
        performanceLabWindow->applyAppearance(&appLookAndFeel, palette.background);
    }
    else
    {
        performanceLabWindow->setVisible(true);
        performanceLabWindow->toFront(true);
    }
}

void MainComponent::automatePerformanceLab(std::function<void(bool)> completion)
{
    performanceLabWindow = std::make_unique<PerformanceLabWindow>(
        [this] { closePerformanceLab(); }, std::move(completion));
    const auto palette = appPaletteFor(currentTheme);
    performanceLabWindow->applyAppearance(&appLookAndFeel, palette.background);
}

void MainComponent::closePerformanceLab()
{
    performanceLabWindow.reset();
}
#endif
