#include "../../../MainComponent.h"

#include "../model/WorkspaceMenuModel.h"

namespace
{
constexpr int toolsMenuWidth = 190;

// One block of four consecutive ids per registered tool, so the menu grows
// with the registry instead of needing a hand-assigned id per tool. The base
// leaves room below it for the fixed items and room above it, before
// firstWorkspaceMenuItemId, for far more tools than the roadmap plans.
constexpr int firstToolMenuItemId = 100;
constexpr int toolMenuItemsPerTool = 4;
constexpr int openToolMenuOffset = 0;
constexpr int dockToolMenuOffset = 1;
constexpr int floatToolMenuOffset = 2;
constexpr int closeToolMenuOffset = 3;

constexpr int horizontalLayoutMenuItemId = 31;
constexpr int verticalLayoutMenuItemId = 32;
constexpr int tabbedLayoutMenuItemId = 33;
constexpr int saveWorkspaceMenuItemId = 60;
constexpr int overwriteWorkspaceMenuItemId = 61;
constexpr int renameWorkspaceMenuItemId = 62;
constexpr int deleteWorkspaceMenuItemId = 63;
constexpr int resetWorkspaceMenuItemId = 64;
constexpr int firstWorkspaceMenuItemId = 1000;
} // namespace

void MainComponent::showToolsMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);

    // Every registered tool gets the same submenu, built from its definition.
    // A new tool appears here by being registered -- there is nothing to add.
    std::vector<ToolInstanceId> menuTools;
    for (const auto& definition : builtInToolRegistry().tools())
    {
        const auto instance = instanceIdToOpen(definition.id);
        const auto* entry = findLiveTool(instance);
        const auto presentation = entry != nullptr ? entry->state.presentation()
                                                   : WorkspaceToolState::Presentation::closed;
        const auto isOpen = entry != nullptr && entry->state.isOpen();
        const auto base =
            firstToolMenuItemId + (static_cast<int>(menuTools.size()) * toolMenuItemsPerTool);

        juce::PopupMenu toolMenu;
        toolMenu.addItem(base + openToolMenuOffset, "Open or focus", true, isOpen);
        toolMenu.addItem(
            base + dockToolMenuOffset, "Dock in workspace", true,
            presentation == WorkspaceToolState::Presentation::docked);
        toolMenu.addItem(
            base + floatToolMenuOffset, "Float in window", true,
            presentation == WorkspaceToolState::Presentation::floating);
        toolMenu.addSeparator();
        toolMenu.addItem(base + closeToolMenuOffset, "Close", isOpen);

        menu.addSubMenu(juce::String(definition.displayName), toolMenu);
        menuTools.push_back(instance);
    }

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

    const auto workspaceModel = WorkspaceMenuModel::fromCatalog(workspaceCatalog);
    juce::PopupMenu workspaceMenu;
    std::vector<std::string> workspaceIds;
    workspaceIds.reserve(workspaceModel.entries.size());
    auto addedNamedSeparator = false;
    for (const auto& entry : workspaceModel.entries)
    {
        if (!entry.builtIn && !addedNamedSeparator)
        {
            workspaceMenu.addSeparator();
            addedNamedSeparator = true;
        }
        const auto menuItemId = firstWorkspaceMenuItemId + static_cast<int>(workspaceIds.size());
        workspaceMenu.addItem(
            menuItemId, juce::String::fromUTF8(entry.name.c_str()), true, entry.selected);
        workspaceIds.push_back(entry.id);
    }
    workspaceMenu.addSeparator();
    workspaceMenu.addItem(saveWorkspaceMenuItemId, "Save As...", workspaceModel.canSaveAs);
    workspaceMenu.addItem(
        overwriteWorkspaceMenuItemId, "Overwrite Current...", workspaceModel.canOverwrite);
    workspaceMenu.addItem(renameWorkspaceMenuItemId, "Rename Current...", workspaceModel.canRename);
    workspaceMenu.addItem(deleteWorkspaceMenuItemId, "Delete Current...", workspaceModel.canDelete);
    workspaceMenu.addSeparator();
    workspaceMenu.addItem(resetWorkspaceMenuItemId, "Reset to Pitch Practice...");
    menu.addSeparator();
    menu.addSubMenu("Workspaces", workspaceMenu);

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetComponent(&toolsButton)
            .withMinimumWidth(toolsMenuWidth),
        [safeThis, workspaceIds = std::move(workspaceIds),
         menuTools = std::move(menuTools)](int selectedItemId)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            const auto toolIndex = (selectedItemId - firstToolMenuItemId) / toolMenuItemsPerTool;
            if (selectedItemId >= firstToolMenuItemId &&
                toolIndex < static_cast<int>(menuTools.size()))
            {
                const auto& instance = menuTools[static_cast<size_t>(toolIndex)];
                switch ((selectedItemId - firstToolMenuItemId) % toolMenuItemsPerTool)
                {
                case openToolMenuOffset:
                    safeThis->openTool(instance);
                    break;
                case dockToolMenuOffset:
                    safeThis->presentTool(instance, WorkspaceToolState::Presentation::docked);
                    break;
                case floatToolMenuOffset:
                    safeThis->presentTool(instance, WorkspaceToolState::Presentation::floating);
                    break;
                default:
                    safeThis->closeTool(instance);
                    break;
                }
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
            else if (
                selectedItemId >= firstWorkspaceMenuItemId &&
                selectedItemId < firstWorkspaceMenuItemId + static_cast<int>(workspaceIds.size()))
            {
                const auto workspaceIndex =
                    static_cast<std::size_t>(selectedItemId - firstWorkspaceMenuItemId);
                safeThis->applyWorkspace(workspaceIds[workspaceIndex]);
            }
            else if (selectedItemId == saveWorkspaceMenuItemId)
            {
                safeThis->showSaveWorkspaceDialog();
            }
            else if (selectedItemId == overwriteWorkspaceMenuItemId)
            {
                safeThis->confirmOverwriteWorkspace();
            }
            else if (selectedItemId == renameWorkspaceMenuItemId)
            {
                safeThis->showRenameWorkspaceDialog();
            }
            else if (selectedItemId == deleteWorkspaceMenuItemId)
            {
                safeThis->confirmDeleteWorkspace();
            }
            else if (selectedItemId == resetWorkspaceMenuItemId)
            {
                safeThis->confirmResetWorkspace();
            }
        });
}
