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
    // What a pane is guaranteed, not what it would prefer.
    //
    // This was 480, which is what a tool wants in order to draw its full
    // presentation. Treated as a floor it made three docked tools demand
    // 480 x 3 + 8 x 2 = 1456px before the workspace would lay out at all --
    // more than the widest legal window, since main.cpp allows the window down
    // to 980 and defaults to 1280. JUCE resolves an impossible minimum by
    // laying the last pane out past the edge, which is why the third tool was
    // clipped at every size and gone entirely at 800x600.
    //
    // A floor is a promise that a pane is never smaller than this, so it has to
    // be a width a tool can still be read at, not the width it would like.
    // 180 is below the tuner's compactDisplayWidth of 320, so a pane at the
    // floor is showing a compact form rather than a squeezed full one. Three
    // panes now need 556px and four need 744, both comfortably inside the 980
    // minimum -- and inside the 640x480 the capture harness uses to stress
    // layouts, which is below what the window manager allows but is exactly
    // where a layout that only just fits stops fitting.
    //
    // See openspec size-docked-panes-to-fit for why this could not move before
    // the tools had something to draw at this width.
    static constexpr int minimumHorizontalPaneSize = 180;
    static constexpr int minimumVerticalPaneSize = 120;
    static constexpr int dividerThickness = 8;

    // The width a split needs for `paneCount` panes side by side, all at the
    // floor. Arithmetic over the tree rather than a rendered layout, so a test
    // can ask it without a display -- which is the only way the defect this
    // replaced would have been caught before a screenshot.
    [[nodiscard]] int minimumForChild(juce::Component* child) const
    {
        return vertical ? minimumHeightOf(child) : minimumWidthOf(child);
    }

    [[nodiscard]] static constexpr int minimumWidthForPanes(int paneCount) noexcept
    {
        return paneCount <= 0
                   ? 0
                   : paneCount * minimumHorizontalPaneSize + (paneCount - 1) * dividerThickness;
    }

    [[nodiscard]] static constexpr int minimumHeightForPanes(int paneCount) noexcept
    {
        return paneCount <= 0
                   ? 0
                   : paneCount * minimumVerticalPaneSize + (paneCount - 1) * dividerThickness;
    }

    WorkspaceSplitPane(
        juce::Component& firstPane,
        juce::Component& secondPane,
        bool isVertical,
        double initialRatio,
        juce::LookAndFeel& lookAndFeelToUse,
        std::function<void(double)> ratioChanged = {})
        : firstChild(&firstPane), secondChild(&secondPane), vertical(isVertical),
          ratio(std::clamp(initialRatio, 0.1, 0.9)), onRatioChanged(std::move(ratioChanged))
    {
        addAndMakeVisible(firstPane);
        addAndMakeVisible(secondPane);

        // Each child's *own* minimum, not one constant for both.
        //
        // A child may be another split, and a split holding two panes needs
        // more than a single pane does. Giving both children the same flat
        // minimum meant this split would hand a nested split its ratio share --
        // half the width -- while that nested split needed rather more, and the
        // overflow came out at the right edge as a tool nobody could see. The
        // flat minimum is why three docked tools misbehaved and two never did.
        layoutManager.setItemLayout(0, minimumForChild(firstChild), -1.0, -ratio);
        layoutManager.setItemLayout(1, dividerThickness, dividerThickness, dividerThickness);
        layoutManager.setItemLayout(2, minimumForChild(secondChild), -1.0, -(1.0 - ratio));

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

    // What this split needs along the axis it divides, counting everything
    // nested inside it. A leaf pane needs the floor; a split needs both its
    // children plus its divider when it divides the same axis, or the larger of
    // them when it divides the other one.
    [[nodiscard]] int minimumWidth() const
    {
        const auto first = minimumWidthOf(firstChild);
        const auto second = minimumWidthOf(secondChild);

        return vertical ? std::max(first, second) : first + dividerThickness + second;
    }

    [[nodiscard]] int minimumHeight() const
    {
        const auto first = minimumHeightOf(firstChild);
        const auto second = minimumHeightOf(secondChild);

        return vertical ? first + dividerThickness + second : std::max(first, second);
    }

    [[nodiscard]] static int minimumWidthOf(juce::Component* child)
    {
        // A split asks about its own type rather than naming a tool -- the
        // distinction the tool registry exists to keep.
        if (const auto* split = dynamic_cast<const WorkspaceSplitPane*>(child))
        {
            return split->minimumWidth();
        }

        // A tabbed group is a leaf here -- its tabs share one pane, so its
        // *content* needs what one pane needs -- but the bar itself is chrome
        // the content never sees, spent off whichever axis the bar lies along.
        // TabsAtLeft/TabsAtRight spend it here; TabsAtTop/TabsAtBottom do not,
        // because JUCE already degrades a bar that runs out of its own axis by
        // shrinking tabs and folding the rest behind an overflow button rather
        // than drawing past the edge -- the graceful case #150 needed but a
        // plain leaf never had.
        if (const auto* tabs = dynamic_cast<const juce::TabbedComponent*>(child))
        {
            const auto barSpendsWidth =
                tabs->getOrientation() == juce::TabbedButtonBar::TabsAtLeft ||
                tabs->getOrientation() == juce::TabbedButtonBar::TabsAtRight;
            return minimumHorizontalPaneSize + (barSpendsWidth ? tabs->getTabBarDepth() : 0);
        }

        return minimumHorizontalPaneSize;
    }

    [[nodiscard]] static int minimumHeightOf(juce::Component* child)
    {
        if (const auto* split = dynamic_cast<const WorkspaceSplitPane*>(child))
        {
            return split->minimumHeight();
        }

        if (const auto* tabs = dynamic_cast<const juce::TabbedComponent*>(child))
        {
            // TabsAtTop is the only orientation this workspace builds
            // (MainComponentWorkspaceLayout.cpp), at a depth of 38 -- read from
            // the component rather than repeated as a second constant that
            // could drift from the one actually configured there.
            const auto barSpendsHeight =
                tabs->getOrientation() == juce::TabbedButtonBar::TabsAtTop ||
                tabs->getOrientation() == juce::TabbedButtonBar::TabsAtBottom;
            return minimumVerticalPaneSize + (barSpendsHeight ? tabs->getTabBarDepth() : 0);
        }

        return minimumVerticalPaneSize;
    }

    // The smallest width and height this split can be given, if it and every
    // split inside it may lay out either way round. What a parent deciding
    // whether to stack needs to know: a nested side-by-side pair that would
    // stack itself needs one pane's width, not two.
    [[nodiscard]] int flexibleMinimumWidth() const
    {
        const auto first = flexibleMinimumWidthOf(firstChild);
        const auto second = flexibleMinimumWidthOf(secondChild);

        return std::min(first + dividerThickness + second, std::max(first, second));
    }

    [[nodiscard]] int flexibleMinimumHeight() const
    {
        const auto first = flexibleMinimumHeightOf(firstChild);
        const auto second = flexibleMinimumHeightOf(secondChild);

        return std::min(first + dividerThickness + second, std::max(first, second));
    }

    void resized() override
    {
        // The arrangement the user made, whenever it fits -- laid out exactly
        // as it always was. Only a split that has run out of room along its own
        // axis does anything else.
        if (fitsAsArranged())
        {
            divider->setVisible(true);
            juce::Component* components[]{firstChild, divider.get(), secondChild};
            layoutManager.layOutComponents(
                components, 3, 0, 0, getWidth(), getHeight(), vertical, true);

            return;
        }

        // Turned the other way when that fits: side by side becomes stacked in
        // a tall, narrow window, and back again when it widens. Otherwise kept
        // the way it was and shared out, smaller than each pane would like but
        // all of it inside the split. Either way the divider is put away: it
        // drags along the arranged axis, and the arrangement it moves is the
        // one that comes back.
        divider->setVisible(false);
        layOutByRatio(fitsTurned() ? !vertical : vertical);
    }

    [[nodiscard]] double currentRatio() const noexcept
    {
        // Out of the arranged layout the divider is not where the panes are,
        // so the ratio is the one kept for when it comes back.
        if (!divider->isVisible())
        {
            return ratio;
        }

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

    // Whether the layout manager can place both children along the arranged
    // axis without either going below what it needs -- the case where it
    // overflowed the split instead.
    [[nodiscard]] bool fitsAsArranged() const
    {
        const auto along = vertical ? getHeight() : getWidth();
        const auto across = vertical ? getWidth() : getHeight();
        const auto needAcross =
            vertical ? std::max(minimumWidthOf(firstChild), minimumWidthOf(secondChild))
                     : std::max(minimumHeightOf(firstChild), minimumHeightOf(secondChild));

        return along >=
                   minimumForChild(firstChild) + dividerThickness + minimumForChild(secondChild) &&
               across >= needAcross;
    }

    // Whether the children fit laid out across the arranged axis instead,
    // counting on nested splits to turn as well where they need to.
    [[nodiscard]] bool fitsTurned() const
    {
        const auto turnedVertical = !vertical;
        const auto along = turnedVertical ? getHeight() : getWidth();
        const auto across = turnedVertical ? getWidth() : getHeight();
        const auto alongOf = [turnedVertical](juce::Component* child)
        { return turnedVertical ? flexibleMinimumHeightOf(child) : flexibleMinimumWidthOf(child); };
        const auto acrossOf = [turnedVertical](juce::Component* child)
        { return turnedVertical ? flexibleMinimumWidthOf(child) : flexibleMinimumHeightOf(child); };

        return along >= alongOf(firstChild) + dividerThickness + alongOf(secondChild) &&
               across >= std::max(acrossOf(firstChild), acrossOf(secondChild));
    }

    // Both children along `alongVertical`, split at the kept ratio: each given
    // what it needs where there is room for both, and the ratio's share of
    // whatever there is where there is not.
    void layOutByRatio(bool alongVertical)
    {
        const auto total = alongVertical ? getHeight() : getWidth();
        const auto available = std::max(0, total - dividerThickness);
        const auto needs = [alongVertical](juce::Component* child)
        { return alongVertical ? flexibleMinimumHeightOf(child) : flexibleMinimumWidthOf(child); };
        const auto firstNeeds = needs(firstChild);
        const auto secondNeeds = needs(secondChild);

        auto firstSize = juce::roundToInt(static_cast<double>(available) * ratio);
        if (firstNeeds + secondNeeds <= available)
        {
            firstSize = std::clamp(firstSize, firstNeeds, available - secondNeeds);
        }
        firstSize = std::clamp(firstSize, 0, available);

        const auto secondStart = firstSize + dividerThickness;
        const auto secondSize = std::max(0, total - secondStart);

        if (alongVertical)
        {
            firstChild->setBounds(0, 0, getWidth(), firstSize);
            secondChild->setBounds(0, secondStart, getWidth(), secondSize);
        }
        else
        {
            firstChild->setBounds(0, 0, firstSize, getHeight());
            secondChild->setBounds(secondStart, 0, secondSize, getHeight());
        }
    }

    [[nodiscard]] static int flexibleMinimumWidthOf(juce::Component* child)
    {
        if (const auto* split = dynamic_cast<const WorkspaceSplitPane*>(child))
        {
            return split->flexibleMinimumWidth();
        }

        return minimumWidthOf(child);
    }

    [[nodiscard]] static int flexibleMinimumHeightOf(juce::Component* child)
    {
        if (const auto* split = dynamic_cast<const WorkspaceSplitPane*>(child))
        {
            return split->flexibleMinimumHeight();
        }

        return minimumHeightOf(child);
    }

    void reportCurrentRatio()
    {
        ratio = currentRatio();

        if (onRatioChanged)
        {
            onRatioChanged(currentRatio());
        }
    }

    juce::Component* firstChild;
    juce::Component* secondChild;
    bool vertical;

    // Where the divider is, as a share of the split. Kept apart from the layout
    // manager so a split that has stacked for want of room, where the divider
    // is put away, still knows where to put it when the room comes back.
    double ratio;
    std::function<void(double)> onRatioChanged;
    juce::StretchableLayoutManager layoutManager;
    std::unique_ptr<ReportingResizerBar> divider;
};
