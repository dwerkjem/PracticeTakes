#pragma once

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
    explicit WorkspaceTabbedComponent(juce::TabbedButtonBar::Orientation orientation)
        : juce::TabbedComponent(orientation)
    {
    }

    void addToolTab(
        const juce::String& tabName,
        juce::Colour tabBackgroundColour,
        juce::Component* contentComponent,
        std::function<void(juce::Component&)> dragHandler)
    {
        tabDragHandlers.push_back(std::move(dragHandler));
        addTab(tabName, tabBackgroundColour, contentComponent, false);
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
    std::vector<std::function<void(juce::Component&)>> tabDragHandlers;
};
