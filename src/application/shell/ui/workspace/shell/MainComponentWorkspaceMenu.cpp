#include "../../../MainComponent.h"

#include "../../../../localisation/Localisation.h"
#include "../model/WorkspaceMenuModel.h"

namespace
{
constexpr int toolsMenuWidth = 190;

// Tool items are numbered from here, one id per item, and looked up in a table
// built alongside the menu -- each submenu's length varies with how many
// instances of its tool are open, so a fixed block per tool cannot work. The
// base leaves room below it for the fixed items and room above it, before
// firstWorkspaceMenuItemId, for far more items than the instance cap allows.
constexpr int firstToolMenuItemId = 100;

enum class ToolMenuAction
{
    openOrFocus,
    openAnother,
    dock,
    floating,
    rename,
    close
};

struct ToolMenuItem
{
    ToolInstanceId instance;
    std::string toolId;
    ToolMenuAction action;
};

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

    // Every registered tool gets a submenu built from its definition, so a new
    // tool appears here by being registered -- there is nothing to add. A tool
    // with at most one open instance shows the familiar four items; once a
    // multi-instance tool has two, each open instance gets its own entry
    // (openspec/changes/archive/2026-11-08-multi-instance-tools).
    std::vector<ToolMenuItem> toolItems;
    const auto addToolItem = [&toolItems](
                                 juce::PopupMenu& into, const juce::String& text, ToolMenuItem item,
                                 bool enabled = true, bool ticked = false)
    {
        into.addItem(
            firstToolMenuItemId + static_cast<int>(toolItems.size()), text, enabled, ticked);
        toolItems.push_back(std::move(item));
    };

    // Dock, float, rename and close for one instance. Dock and Float are the
    // state -- one of them ticked says it is open and where; neither ticked says
    // it is closed.
    const auto addInstanceItems =
        [this, &addToolItem](juce::PopupMenu& into, const ToolInstanceId& instance)
    {
        const auto* entry = findLiveTool(instance);
        const auto presentation = entry != nullptr ? entry->state.presentation()
                                                   : WorkspaceToolState::Presentation::closed;
        const auto isOpen = entry != nullptr && entry->state.isOpen();
        const auto toolId = std::string{instance.toolId()};
        addToolItem(
            into, l10n::text("menu.tools.dock"), {instance, toolId, ToolMenuAction::dock}, true,
            presentation == WorkspaceToolState::Presentation::docked);
        addToolItem(
            into, l10n::text("menu.tools.float"), {instance, toolId, ToolMenuAction::floating},
            true, presentation == WorkspaceToolState::Presentation::floating);
        addToolItem(
            into, l10n::text("menu.tools.rename"), {instance, toolId, ToolMenuAction::rename},
            isOpen);
        into.addSeparator();
        addToolItem(
            into, l10n::text("menu.tools.close"), {instance, toolId, ToolMenuAction::close},
            isOpen);
    };

    for (const auto& definition : builtInToolRegistry().tools())
    {
        const auto isMulti = definition.instancePolicy == ToolInstancePolicy::multi;
        const auto openInstances = openInstancesOf(definition.id);
        const auto focusTarget = instanceIdToOpen(definition.id);

        juce::PopupMenu toolMenu;
        // "Open or focus" is an action, not a state, so it carries no tick.
        addToolItem(
            toolMenu, l10n::text("menu.tools.open-or-focus"),
            {focusTarget, definition.id, ToolMenuAction::openOrFocus});
        if (isMulti)
        {
            // Its reason is part of its text, so a disabled item is never a
            // mystery. Out of audio streams it stays clickable: choosing it
            // says what is using them, which a greyed-out item cannot.
            const auto verdict = canOpenAnotherInstance(definition.id);
            juce::String text = l10n::text("menu.tools.open-another");
            if (verdict == ToolInstanceAllocator::OpenAnother::toolLimitReached)
            {
                text = l10n::format(
                    "menu.tools.open-another.at-most",
                    {{"most",
                      l10n::number(
                          static_cast<double>(ToolInstanceAllocator::maximumInstancesPerTool),
                          0)}});
            }
            else if (verdict == ToolInstanceAllocator::OpenAnother::noAudioStreamFree)
            {
                text = l10n::text("menu.tools.open-another.no-audio-stream");
            }
            addToolItem(
                toolMenu, text, {{}, definition.id, ToolMenuAction::openAnother},
                verdict != ToolInstanceAllocator::OpenAnother::toolLimitReached);
        }

        if (isMulti && openInstances.size() >= 2)
        {
            toolMenu.addSeparator();
            for (const auto& instance : openInstances)
            {
                juce::PopupMenu instanceMenu;
                addInstanceItems(instanceMenu, instance);
                toolMenu.addSubMenu(toolName(instance), instanceMenu);
            }
        }
        else
        {
            addInstanceItems(toolMenu, focusTarget);
        }

        menu.addSubMenu(juce::String(definition.displayName), toolMenu);
    }

    const auto dockedCount = dockedTools().size();
    const auto canArrangeSplit = workspaceLayoutState.canSplitRoot();
    juce::PopupMenu layoutMenu;
    layoutMenu.addItem(
        horizontalLayoutMenuItemId, l10n::text("menu.arrange.side-by-side"), canArrangeSplit,
        workspaceLayoutState.isRootOrientation(WorkspaceLayoutState::Orientation::horizontal));
    layoutMenu.addItem(
        verticalLayoutMenuItemId, l10n::text("menu.arrange.stacked"), canArrangeSplit,
        workspaceLayoutState.isRootOrientation(WorkspaceLayoutState::Orientation::vertical));
    layoutMenu.addItem(
        tabbedLayoutMenuItemId, l10n::text("menu.arrange.tabbed"), dockedCount >= 2,
        workspaceLayoutState.isFlattenedTabs());
    menu.addSeparator();
    menu.addSubMenu(l10n::text("menu.arrange"), layoutMenu, canArrangeSplit || dockedCount >= 2);

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
            menuItemId,
            entry.builtIn ? l10n::text(WorkspaceBuiltIns::nameKey(entry.id))
                          : juce::String::fromUTF8(entry.name.c_str()),
            true, entry.selected);
        workspaceIds.push_back(entry.id);
    }
    workspaceMenu.addSeparator();
    workspaceMenu.addItem(
        saveWorkspaceMenuItemId, l10n::text("menu.workspaces.save-as"), workspaceModel.canSaveAs);
    workspaceMenu.addItem(
        overwriteWorkspaceMenuItemId, l10n::text("menu.workspaces.overwrite"),
        workspaceModel.canOverwrite);
    workspaceMenu.addItem(
        renameWorkspaceMenuItemId, l10n::text("menu.workspaces.rename"), workspaceModel.canRename);
    workspaceMenu.addItem(
        deleteWorkspaceMenuItemId, l10n::text("menu.workspaces.delete"), workspaceModel.canDelete);
    workspaceMenu.addSeparator();
    workspaceMenu.addItem(
        resetWorkspaceMenuItemId,
        l10n::format(
            "menu.workspaces.reset",
            {{"workspace",
              l10n::text(WorkspaceBuiltIns::nameKey(WorkspaceBuiltIns::pitchPractice().id))}}));
    menu.addSeparator();
    menu.addSubMenu(l10n::text("menu.workspaces"), workspaceMenu);

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetComponent(&toolsButton)
            .withMinimumWidth(toolsMenuWidth),
        [safeThis, workspaceIds = std::move(workspaceIds),
         toolItems = std::move(toolItems)](int selectedItemId)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            const auto toolIndex = selectedItemId - firstToolMenuItemId;
            if (selectedItemId >= firstToolMenuItemId &&
                toolIndex < static_cast<int>(toolItems.size()))
            {
                const auto& item = toolItems[static_cast<std::size_t>(toolIndex)];
                switch (item.action)
                {
                case ToolMenuAction::openOrFocus:
                    safeThis->openTool(item.instance);
                    break;
                case ToolMenuAction::openAnother:
                    safeThis->openAnotherInstance(item.toolId);
                    break;
                case ToolMenuAction::dock:
                    safeThis->presentTool(item.instance, WorkspaceToolState::Presentation::docked);
                    break;
                case ToolMenuAction::floating:
                    safeThis->presentTool(
                        item.instance, WorkspaceToolState::Presentation::floating);
                    break;
                case ToolMenuAction::rename:
                    safeThis->showRenameToolDialog(item.instance);
                    break;
                case ToolMenuAction::close:
                    safeThis->closeTool(item.instance);
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
