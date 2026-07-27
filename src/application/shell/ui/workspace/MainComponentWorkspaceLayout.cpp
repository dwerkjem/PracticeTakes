#include "../../MainComponent.h"

#include "DockedToolPanel.h"

#include <utility>

namespace
{
constexpr int minimumHorizontalToolSize = 480;
constexpr int minimumVerticalToolSize = 280;
constexpr int dividerSize = 8;
} // namespace

void MainComponent::setWorkspaceLayout(WorkspaceLayoutState::Layout layout)
{
    workspaceLayoutState.setLayout(layout);
    for (const auto tool : allToolTypes)
        if (stateFor(tool).isOpen())
            presentTool(tool, WorkspaceToolState::Presentation::docked);
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

void MainComponent::rebuildWorkspaceContainer()
{
    if (workspaceTabs != nullptr)
    {
        while (workspaceTabs->getNumTabs() > 0)
            workspaceTabs->removeTab(0);
        removeChildComponent(workspaceTabs.get());
        workspaceTabs.reset();
    }

    if (workspaceDivider != nullptr)
    {
        removeChildComponent(workspaceDivider.get());
        workspaceDivider.reset();
    }

    for (const auto tool : allToolTypes)
    {
        auto& dock = dockFor(tool);
        if (dock != nullptr && dock->getParentComponent() == this)
            removeChildComponent(dock.get());
    }

    const auto docked = dockedTools();
    if (docked.empty())
    {
        resized();
        return;
    }

    // A tile only ever has room for two tools. Three or more docked tools
    // always share a single tab strip, regardless of which specific tools
    // they are; this is what lets any number of tools (including ones added
    // in the future) participate without bespoke handling here.
    if (docked.size() > 2 || workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::tabbed)
    {
        workspaceTabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        workspaceTabs->setTabBarDepth(38);
        workspaceTabs->setLookAndFeel(&appLookAndFeel);
        addAndMakeVisible(*workspaceTabs);

        int activeIndex = 0;
        for (size_t index = 0; index < docked.size(); ++index)
        {
            const auto tool = docked[index];
            if (tool == currentTool)
                activeIndex = static_cast<int>(index);
            workspaceTabs->addTab(
                toolName(tool), appPaletteFor(currentTheme).panel, dockFor(tool).get(), false);
        }
        workspaceTabs->setCurrentTabIndex(activeIndex);
    }
    else
    {
        for (const auto tool : docked)
            addAndMakeVisible(*dockFor(tool));

        if (docked.size() == 2)
        {
            workspaceLayoutManager.clearAllItems();
            const auto vertical =
                workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::vertical;
            const auto minimumSize = vertical ? minimumVerticalToolSize : minimumHorizontalToolSize;
            workspaceLayoutManager.setItemLayout(0, minimumSize, -1.0, -0.5);
            workspaceLayoutManager.setItemLayout(1, dividerSize, dividerSize, dividerSize);
            workspaceLayoutManager.setItemLayout(2, minimumSize, -1.0, -0.5);
            workspaceDivider = std::make_unique<juce::StretchableLayoutResizerBar>(
                &workspaceLayoutManager, 1, !vertical);
            workspaceDivider->setLookAndFeel(&appLookAndFeel);
            addAndMakeVisible(*workspaceDivider);
        }
    }

    resized();
}

void MainComponent::layoutWorkspace(juce::Rectangle<int> bounds)
{
    if (workspaceTabs != nullptr)
    {
        workspaceTabs->setBounds(bounds);
        return;
    }

    const auto docked = dockedTools();
    if (docked.size() == 2 && workspaceDivider != nullptr)
    {
        auto first = docked[0];
        auto second = docked[1];
        if (static_cast<ToolType>(workspaceLayoutState.first()) == second)
            std::swap(first, second);

        juce::Component* components[]{
            dockFor(first).get(), workspaceDivider.get(), dockFor(second).get()};
        workspaceLayoutManager.layOutComponents(
            components, 3, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
            workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::vertical, true);
    }
    else if (docked.size() == 1)
    {
        dockFor(docked[0])->setBounds(bounds);
    }
}
