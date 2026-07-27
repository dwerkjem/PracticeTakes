#pragma once

#include "../../MainComponent.h"

#include <memory>

// A single two-way split in the fractal workspace tree: two child panes side
// by side (or stacked), separated by a draggable divider. A child pane may
// itself be another WorkspaceSplitPane (or a tabbed group), which is what
// lets the workspace keep subdividing to any depth -- this component only
// ever has to lay out its own two direct children; JUCE's normal
// setBounds()-cascades-to-resized() behaviour takes care of the rest
// recursively.
class MainComponent::WorkspaceSplitPane final : public juce::Component
{
  public:
    static constexpr int minimumHorizontalPaneSize = 480;
    static constexpr int minimumVerticalPaneSize = 280;
    static constexpr int dividerThickness = 8;

    WorkspaceSplitPane(
        juce::Component& firstPane,
        juce::Component& secondPane,
        bool isVertical,
        juce::LookAndFeel& lookAndFeelToUse)
        : firstChild(&firstPane), secondChild(&secondPane), vertical(isVertical)
    {
        addAndMakeVisible(firstPane);
        addAndMakeVisible(secondPane);

        const auto minimumSize = vertical ? minimumVerticalPaneSize : minimumHorizontalPaneSize;
        layoutManager.setItemLayout(0, minimumSize, -1.0, -0.5);
        layoutManager.setItemLayout(1, dividerThickness, dividerThickness, dividerThickness);
        layoutManager.setItemLayout(2, minimumSize, -1.0, -0.5);

        divider = std::make_unique<juce::StretchableLayoutResizerBar>(&layoutManager, 1, !vertical);
        divider->setLookAndFeel(&lookAndFeelToUse);
        addAndMakeVisible(*divider);
    }

    ~WorkspaceSplitPane() override
    {
        // The panes shown here are owned elsewhere (a per-tool dock, or
        // another entry in MainComponent::workspaceContainers); detach them
        // rather than let ~Component silently do nothing, so a stale parent
        // pointer never lingers on a component that outlives this pane.
        if (firstChild != nullptr)
            removeChildComponent(firstChild);
        if (secondChild != nullptr)
            removeChildComponent(secondChild);
    }

    void resized() override
    {
        juce::Component* components[]{firstChild, divider.get(), secondChild};
        layoutManager.layOutComponents(
            components, 3, 0, 0, getWidth(), getHeight(), vertical, true);
    }

  private:
    juce::Component* firstChild;
    juce::Component* secondChild;
    bool vertical;
    juce::StretchableLayoutManager layoutManager;
    std::unique_ptr<juce::StretchableLayoutResizerBar> divider;
};
