#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

// A single two-way split in the fractal workspace tree: two child panes side
// by side (or stacked), separated by a draggable divider. A child pane may
// itself be another WorkspaceSplitPane (or a tabbed group), which is what
// lets the workspace keep subdividing to any depth -- this component only
// ever has to lay out its own two direct children; JUCE's normal
// setBounds()-cascades-to-resized() behaviour takes care of the rest
// recursively.
class WorkspaceSplitPane final : public juce::Component
{
  public:
    static constexpr int minimumHorizontalPaneSize = 480;
    static constexpr int minimumVerticalPaneSize = 280;
    static constexpr int dividerThickness = 8;

    WorkspaceSplitPane(
        juce::Component& firstPane,
        juce::Component& secondPane,
        bool isVertical,
        double initialRatio,
        juce::LookAndFeel& lookAndFeelToUse,
        std::function<void(double)> ratioChanged = {})
        : firstChild(&firstPane), secondChild(&secondPane), vertical(isVertical),
          onRatioChanged(std::move(ratioChanged))
    {
        addAndMakeVisible(firstPane);
        addAndMakeVisible(secondPane);

        const auto minimumSize = vertical ? minimumVerticalPaneSize : minimumHorizontalPaneSize;
        const auto ratio = std::clamp(initialRatio, 0.1, 0.9);
        layoutManager.setItemLayout(0, minimumSize, -1.0, -ratio);
        layoutManager.setItemLayout(1, dividerThickness, dividerThickness, dividerThickness);
        layoutManager.setItemLayout(2, minimumSize, -1.0, -(1.0 - ratio));

        divider = std::make_unique<ReportingResizerBar>(
            layoutManager, !vertical,
            [this]
            {
                resized();
                reportCurrentRatio();
            });
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
        {
            removeChildComponent(firstChild);
        }
        if (secondChild != nullptr)
        {
            removeChildComponent(secondChild);
        }
    }

    void resized() override
    {
        juce::Component* components[]{firstChild, divider.get(), secondChild};
        layoutManager.layOutComponents(
            components, 3, 0, 0, getWidth(), getHeight(), vertical, true);
    }

    [[nodiscard]] double currentRatio() const noexcept
    {
        const auto availableSize = availableSplitSize();
        if (availableSize <= 0)
        {
            return 0.5;
        }
        return std::clamp(
            static_cast<double>(layoutManager.getItemCurrentAbsoluteSize(0)) /
                static_cast<double>(availableSize),
            0.1, 0.9);
    }

    void moveDividerTo(int position)
    {
        layoutManager.setItemPosition(1, position);
        resized();
        reportCurrentRatio();
    }

  private:
    class ReportingResizerBar final : public juce::StretchableLayoutResizerBar
    {
      public:
        ReportingResizerBar(
            juce::StretchableLayoutManager& layout,
            bool isVertical,
            std::function<void()> moved)
            : juce::StretchableLayoutResizerBar(&layout, 1, isVertical), onMoved(std::move(moved))
        {
        }

        void hasBeenMoved() override
        {
            if (onMoved)
            {
                onMoved();
            }
        }

      private:
        std::function<void()> onMoved;
    };

    [[nodiscard]] int availableSplitSize() const noexcept
    {
        return std::max(0, (vertical ? getHeight() : getWidth()) - dividerThickness);
    }

    void reportCurrentRatio()
    {
        if (onRatioChanged)
        {
            onRatioChanged(currentRatio());
        }
    }

    juce::Component* firstChild;
    juce::Component* secondChild;
    bool vertical;
    std::function<void(double)> onRatioChanged;
    juce::StretchableLayoutManager layoutManager;
    std::unique_ptr<ReportingResizerBar> divider;
};
