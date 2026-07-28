#pragma once

#include "../model/WorkspaceLayoutState.h"
#include "WorkspaceTabBarButton.h"

#include <functional>
#include <utility>
#include <vector>

// A TabbedComponent whose tab buttons are WorkspaceTabBarButtons: dragging a
// tab label past the drag threshold (or with Ctrl held) starts a workspace
// tool drag for that tab, exactly like the drag handle, docked panel, and
// floating window drag sources. Use addToolTab() instead of the inherited
// addTab() so each tab's drag handler is recorded before JUCE constructs its
// button (TabbedComponent::addTab calls createTabButton() synchronously).
class WorkspaceTabbedComponent final : public juce::TabbedComponent
{
  public:
    using Tool = WorkspaceLayoutState::Tool;

    explicit WorkspaceTabbedComponent(
        juce::TabbedButtonBar::Orientation orientation,
        std::function<void(Tool)> activeToolChanged = {})
        : juce::TabbedComponent(orientation), onActiveToolChanged(std::move(activeToolChanged))
    {
    }

    void addToolTab(
        Tool tool,
        const juce::String& tabName,
        juce::Colour tabBackgroundColour,
        juce::Component* contentComponent,
        std::function<void(juce::Component&)> dragHandler)
    {
        tabTools.push_back(tool);
        tabDragHandlers.push_back(std::move(dragHandler));
        suppressSelectionCallback = true;
        addTab(tabName, tabBackgroundColour, contentComponent, false);
        suppressSelectionCallback = false;
    }

    void restoreActiveTool(Tool tool)
    {
        const auto found = std::find(tabTools.begin(), tabTools.end(), tool);
        if (found == tabTools.end())
        {
            return;
        }

        const auto index = static_cast<int>(std::distance(tabTools.begin(), found));
        suppressSelectionCallback = true;
        setCurrentTabIndex(index, false);
        suppressSelectionCallback = false;
    }

    void currentTabChanged(int newCurrentTabIndex, const juce::String&) override
    {
        if (!suppressSelectionCallback && onActiveToolChanged && newCurrentTabIndex >= 0 &&
            static_cast<size_t>(newCurrentTabIndex) < tabTools.size())
        {
            onActiveToolChanged(tabTools[static_cast<size_t>(newCurrentTabIndex)]);
        }
    }

  protected:
    juce::TabBarButton* createTabButton(const juce::String& tabName, int tabIndex) override
    {
        auto handler = (tabIndex >= 0 && static_cast<size_t>(tabIndex) < tabDragHandlers.size())
                           ? tabDragHandlers[static_cast<size_t>(tabIndex)]
                           : std::function<void(juce::Component&)>{};
        return new WorkspaceTabBarButton(tabName, getTabbedButtonBar(), std::move(handler));
    }

  private:
    std::function<void(Tool)> onActiveToolChanged;
    std::vector<Tool> tabTools;
    std::vector<std::function<void(juce::Component&)>> tabDragHandlers;
    bool suppressSelectionCallback = false;
};
