#include "../../MainComponent.h"

#include "DockedToolPanel.h"
#include "WorkspaceSplitPane.h"
#include "WorkspaceTabbedComponent.h"

#include <algorithm>
#include <utility>

void MainComponent::setWorkspaceLayout(WorkspaceLayoutState::Layout layout)
{
    workspaceLayoutState.applyLayoutCommand(layout);
    rebuildWorkspaceContainer();
}

std::vector<MainComponent::ToolType> MainComponent::dockedTools()
{
    std::vector<ToolType> docked;
    for (const auto tool : allToolTypes)
        if (stateFor(tool).presentation() == WorkspaceToolState::Presentation::docked &&
            dockFor(tool) != nullptr)
            docked.push_back(tool);
    return docked;
}

// Recursively turns a WorkspaceLayoutState::Node into the JUCE component(s)
// that display it, storing any newly created containers in
// workspaceContainers so they stay alive for as long as this shape of the
// tree is on screen. A leaf resolves straight to its tool's dock; a tabs
// node becomes one TabbedComponent; a split node becomes a WorkspaceSplitPane
// wrapping its two (recursively built) children -- which may themselves be
// further splits or tab groups, allowing the workspace to subdivide to any
// depth.
juce::Component* MainComponent::buildWorkspaceNode(const WorkspaceLayoutState::Node* node)
{
    if (node == nullptr)
        return nullptr;

    if (node->kind == WorkspaceLayoutState::NodeKind::leaf)
        return dockFor(static_cast<ToolType>(node->tool)).get();

    if (node->kind == WorkspaceLayoutState::NodeKind::tabs)
    {
        auto tabs = std::make_unique<WorkspaceTabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        tabs->setTabBarDepth(38);
        tabs->setLookAndFeel(&appLookAndFeel);

        // Prefer showing whichever tab matches the app's globally focused
        // tool (if this group happens to contain it); otherwise fall back to
        // this group's own remembered active tab.
        const auto currentToolAsTool = static_cast<WorkspaceLayoutState::Tool>(currentTool);
        const auto preferCurrentTool =
            std::find(node->tabs.begin(), node->tabs.end(), currentToolAsTool) != node->tabs.end();
        const auto preferredActive = preferCurrentTool ? currentToolAsTool : node->activeTab;

        const auto safeThis = juce::Component::SafePointer<MainComponent>(this);

        int activeIndex = 0;
        for (size_t index = 0; index < node->tabs.size(); ++index)
        {
            const auto tool = static_cast<ToolType>(node->tabs[index]);
            if (node->tabs[index] == preferredActive)
                activeIndex = static_cast<int>(index);
            // Dragging this tab's label is a workspace tool drag like any
            // other drag source, but with a "picked up" drag image -- a
            // snapshot of the tab button itself -- instead of the default
            // empty image the other sources use.
            const auto dragHandler = [safeThis, tool](juce::Component& source)
            {
                if (safeThis != nullptr)
                    safeThis->beginToolDrag(
                        tool, source,
                        juce::ScaledImage(source.createComponentSnapshot(source.getLocalBounds())));
            };
            tabs->addToolTab(
                toolName(tool), appPaletteFor(currentTheme).panel, dockFor(tool).get(),
                dragHandler);
        }
        tabs->setCurrentTabIndex(activeIndex);

        auto* rawTabs = tabs.get();
        workspaceContainers.push_back(std::move(tabs));
        return rawTabs;
    }

    auto* first = buildWorkspaceNode(node->first.get());
    auto* second = buildWorkspaceNode(node->second.get());
    if (first == nullptr)
        return second;
    if (second == nullptr)
        return first;

    auto pane = std::make_unique<WorkspaceSplitPane>(
        *first, *second, node->orientation == WorkspaceLayoutState::Orientation::vertical,
        appLookAndFeel);
    auto* rawPane = pane.get();
    workspaceContainers.push_back(std::move(pane));
    return rawPane;
}

void MainComponent::rebuildWorkspaceContainer()
{
    if (workspaceRoot != nullptr)
    {
        removeChildComponent(workspaceRoot);
        workspaceRoot = nullptr;
    }

    // TabbedComponent needs its tabs explicitly cleared before it's
    // destroyed, or its content components (owned by the per-tool docks, not
    // by the tab strip) are left in an inconsistent parented state.
    for (auto& container : workspaceContainers)
        if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(container.get()))
            while (tabs->getNumTabs() > 0)
                tabs->removeTab(0);
    workspaceContainers.clear();

    // Belt-and-braces: whatever the previous tree shape was, make sure every
    // dock is detached from its actual current parent before the tree is
    // rebuilt from scratch below.
    for (const auto tool : allToolTypes)
    {
        auto& dock = dockFor(tool);
        if (dock != nullptr)
            if (auto* parent = dock->getParentComponent())
                parent->removeChildComponent(dock.get());
    }

    workspaceRoot = buildWorkspaceNode(workspaceLayoutState.rootNode());
    if (workspaceRoot != nullptr)
        addAndMakeVisible(*workspaceRoot);

    resized();
}

void MainComponent::layoutWorkspace(juce::Rectangle<int> bounds)
{
    // Recursive layout is handled for free by JUCE: setting the root's
    // bounds triggers its resized(), which lays out its own direct children
    // and sets their bounds, which triggers their resized() in turn, and so
    // on down through however many nested WorkspaceSplitPane/TabbedComponent
    // levels the current tree has.
    if (workspaceRoot != nullptr)
        workspaceRoot->setBounds(bounds);
}
