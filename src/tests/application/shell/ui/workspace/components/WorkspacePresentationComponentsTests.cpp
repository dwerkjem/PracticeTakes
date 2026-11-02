#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/components/WorkspaceSplitPane.h"
#include "application/shell/ui/workspace/components/WorkspaceTabBarButton.h"
#include "application/shell/ui/workspace/components/WorkspaceTabbedComponent.h"

#include <optional>

namespace
{
using Tool = WorkspaceLayoutState::Tool;
constexpr Tool tuner{0};
constexpr Tool spectrogram{1};
} // namespace

TEST_CASE("workspace split pane restores and reports its divider ratio", "[workspace][components]")
{
    juce::Component first;
    juce::Component second;
    juce::LookAndFeel_V4 lookAndFeel;
    std::optional<double> reportedRatio;
    WorkspaceSplitPane pane(
        first, second, false, 0.65, lookAndFeel,
        [&reportedRatio](double ratio) { reportedRatio = ratio; });

    pane.setBounds(0, 0, 1600, 600);

    CHECK(pane.currentRatio() == Catch::Approx(0.65).margin(0.01));
    CHECK_FALSE(reportedRatio.has_value());

    pane.moveDividerTo(900);

    REQUIRE(reportedRatio.has_value());
    CHECK(*reportedRatio == Catch::Approx(pane.currentRatio()));
    CHECK(*reportedRatio == Catch::Approx(900.0 / 1592.0).margin(0.01));
}

TEST_CASE(
    "workspace tabs restore and report selection without losing draggable buttons",
    "[workspace][components]")
{
    std::optional<Tool> selected;
    WorkspaceTabbedComponent tabs(
        juce::TabbedButtonBar::TabsAtTop, [&selected](Tool tool) { selected = tool; });
    juce::Component tunerContent;
    juce::Component spectrogramContent;

    tabs.addToolTab(tuner, "Tuner", juce::Colours::black, &tunerContent, [](auto&) {});
    tabs.addToolTab(
        spectrogram, "Spectrogram", juce::Colours::black, &spectrogramContent, [](auto&) {});
    tabs.restoreActiveTool(spectrogram);

    CHECK(tabs.getCurrentTabIndex() == 1);
    CHECK_FALSE(selected.has_value());
    CHECK(
        dynamic_cast<WorkspaceTabBarButton*>(tabs.getTabbedButtonBar().getTabButton(0)) != nullptr);

    tabs.setCurrentTabIndex(0, true);

    REQUIRE(selected.has_value());
    CHECK(*selected == tuner);
}
// A docked tool that is off the edge of the window is invisible, and nothing in
// the application says so -- no warning, no scroll bar, no gap where it should
// be. #150 was found by a person noticing that a capture of three docked tools
// came out byte-identical to a capture of two, which is a long way round for
// arithmetic that fits on one line.
//
// These check that arithmetic, so the next time a floor is raised the build
// fails on a machine with no display.

TEST_CASE(
    "panes at the floor fit the narrowest window the application allows",
    "[workspace][sizing]")
{
    // main.cpp: setResizeLimits(980, 600, 3200, 2200). Repeated rather than
    // shared, because the point is that these two numbers must agree and a
    // shared constant would let them agree while both being wrong.
    constexpr int narrowestWindow = 980;
    constexpr int shortestWindow = 600;

    // Four is past anything the tool catalogue offers today, which is what
    // makes it the interesting case: the floor should not be sized to exactly
    // the number of tools that happen to exist.
    for (const auto paneCount : {2, 3, 4})
    {
        CHECK(WorkspaceSplitPane::minimumWidthForPanes(paneCount) <= narrowestWindow);
        CHECK(WorkspaceSplitPane::minimumHeightForPanes(paneCount) <= shortestWindow);
    }
}

TEST_CASE("three docked panes fit where they used to overflow", "[workspace][sizing]")
{
    // The exact defect. The old floor of 480 needed 1456px for three panes,
    // which no legal window could give, so the third was laid out past the
    // right edge -- visible as a clipped tool at 1600, and no tool at all at
    // 800x600.
    constexpr int oldFloor = 480;
    constexpr int threePanesAtOldFloor = 3 * oldFloor + 2 * WorkspaceSplitPane::dividerThickness;

    CHECK(threePanesAtOldFloor == 1456);
    CHECK(threePanesAtOldFloor > 980);
    CHECK(WorkspaceSplitPane::minimumWidthForPanes(3) < 800);
}

TEST_CASE(
    "the floor leaves room for a compact tool rather than a squeezed one",
    "[workspace][sizing]")
{
    // A floor is a promise that a pane is never narrower than this, so it has
    // to be a width a tool can be *read* at. The tuner switches to its compact
    // form below 320px; a floor above that would guarantee panes that are too
    // narrow for the full display and too wide to have switched away from it.
    CHECK(WorkspaceSplitPane::minimumHorizontalPaneSize < 320);

    // And not so small that "fits" stops meaning anything.
    CHECK(WorkspaceSplitPane::minimumHorizontalPaneSize >= 120);
}

// A tabbed group is a leaf to WorkspaceSplitPane -- its tabs share one pane --
// but it is not *just* a pane: TabsAtTop spends part of whatever height it is
// given on the tab bar before its content sees any of it. Treating it as a
// plain leaf understated what it needs by exactly the bar's depth, the same
// shape of bug #150 was: a component's real minimum quietly larger than the
// flat constant standing in for it.
TEST_CASE(
    "a tabbed group needs its own bar's depth on top of the pane floor",
    "[workspace][sizing][tabs]")
{
    juce::LookAndFeel_V4 lookAndFeel;
    juce::Component firstTabContentA;
    juce::Component secondTabContentA;
    juce::TabbedComponent tabsA(juce::TabbedButtonBar::TabsAtTop);
    tabsA.setTabBarDepth(38);
    tabsA.addTab("First", juce::Colours::black, &firstTabContentA, false);
    tabsA.addTab("Second", juce::Colours::black, &secondTabContentA, false);

    // Stacked (vertical split): the tab bar competes with the content for
    // height, so the group's minimum height must cover both.
    juce::Component siblingA;
    WorkspaceSplitPane stacked(tabsA, siblingA, true, 0.5, lookAndFeel);

    CHECK(
        stacked.minimumForChild(&tabsA) ==
        WorkspaceSplitPane::minimumVerticalPaneSize + tabsA.getTabBarDepth());

    // Side by side (horizontal split), a second group so it is not reparented
    // out of `stacked` mid-test: TabsAtTop spends no width on the bar -- a tab
    // bar that runs out of width shrinks its tabs and folds the rest behind an
    // overflow button (JUCE's own TabbedButtonBar::resized), which is the
    // graceful degradation #150 needed and a plain leaf never had. The group's
    // minimum width is exactly the plain floor, not inflated by a depth spent
    // on the other axis.
    juce::Component firstTabContentB;
    juce::TabbedComponent tabsB(juce::TabbedButtonBar::TabsAtTop);
    tabsB.setTabBarDepth(38);
    tabsB.addTab("First", juce::Colours::black, &firstTabContentB, false);

    juce::Component siblingB;
    WorkspaceSplitPane sideBySide(tabsB, siblingB, false, 0.5, lookAndFeel);

    CHECK(sideBySide.minimumForChild(&tabsB) == WorkspaceSplitPane::minimumHorizontalPaneSize);
}

// --- Running out of room along the split ------------------------------------
//
// Found by the capture pass's "slim" size, 340x780: two tools side by side need
// 368px, so the second was laid out past the right edge and a third never
// appeared. A split that does not fit along its own axis but would fit across
// it stacks instead, and goes back when the room returns; one that fits neither
// way shares what there is. Either way, nothing is outside the split.

namespace
{
// `leaf`'s bounds in `root`'s coordinates, through any splits in between.
[[nodiscard]] juce::Rectangle<int> boundsIn(juce::Component& root, juce::Component& leaf)
{
    return root.getLocalArea(&leaf, leaf.getLocalBounds());
}

void requireInside(juce::Component& root, std::initializer_list<juce::Component*> leaves)
{
    for (auto* leaf : leaves)
    {
        const auto bounds = boundsIn(root, *leaf);
        INFO("leaf at " << bounds.toString() << " in " << root.getLocalBounds().toString());
        CHECK_FALSE(bounds.isEmpty());
        CHECK(root.getLocalBounds().contains(bounds));
    }
}
} // namespace

TEST_CASE("side-by-side panes stack when the split is too narrow for them", "[workspace][sizing]")
{
    juce::Component first;
    juce::Component second;
    juce::LookAndFeel_V4 lookAndFeel;
    WorkspaceSplitPane pane(first, second, false, 0.5, lookAndFeel);

    pane.setBounds(0, 0, 304, 730);

    requireInside(pane, {&first, &second});
    CHECK(first.getBottom() <= second.getY());
    CHECK(first.getWidth() == 304);
    CHECK(second.getWidth() == 304);
}

TEST_CASE("three side-by-side tools all fit a slim window", "[workspace][sizing]")
{
    juce::Component tuner;
    juce::Component spectrogram;
    juce::Component harmonics;
    juce::LookAndFeel_V4 lookAndFeel;
    WorkspaceSplitPane inner(spectrogram, harmonics, false, 0.5, lookAndFeel);
    WorkspaceSplitPane outer(tuner, inner, false, 1.0 / 3.0, lookAndFeel);

    outer.setBounds(0, 0, 304, 730);

    requireInside(outer, {&tuner, &spectrogram, &harmonics});

    // Stacked, each across the whole width, none on top of another.
    const auto a = boundsIn(outer, tuner);
    const auto b = boundsIn(outer, spectrogram);
    const auto c = boundsIn(outer, harmonics);
    CHECK(a.getBottom() <= b.getY());
    CHECK(b.getBottom() <= c.getY());
    CHECK(a.getWidth() == 304);
    CHECK(c.getWidth() == 304);
}

TEST_CASE("panes go back side by side when the room returns", "[workspace][sizing]")
{
    juce::Component first;
    juce::Component second;
    juce::LookAndFeel_V4 lookAndFeel;
    WorkspaceSplitPane pane(first, second, false, 0.65, lookAndFeel);

    pane.setBounds(0, 0, 304, 730);

    // Asked while stacked -- a workspace saved now must not record a divider
    // position worked out from where the panes happen to be.
    CHECK(pane.currentRatio() == Catch::Approx(0.65).margin(0.01));

    pane.setBounds(0, 0, 1600, 600);

    CHECK(first.getRight() <= second.getX());
    CHECK(first.getY() == second.getY());
    // The divider's place is kept through the detour, not reset.
    CHECK(pane.currentRatio() == Catch::Approx(0.65).margin(0.01));
}

TEST_CASE("panes that fit neither way share what there is", "[workspace][sizing]")
{
    juce::Component first;
    juce::Component second;
    juce::LookAndFeel_V4 lookAndFeel;
    WorkspaceSplitPane pane(first, second, false, 0.5, lookAndFeel);

    // Too narrow for two panes side by side, too short for two stacked.
    pane.setBounds(0, 0, 300, 100);

    requireInside(pane, {&first, &second});
    CHECK(first.getRight() <= second.getX());
}

TEST_CASE("a split with room lays out exactly as it always did", "[workspace][sizing]")
{
    juce::Component first;
    juce::Component second;
    juce::LookAndFeel_V4 lookAndFeel;
    WorkspaceSplitPane pane(first, second, false, 0.5, lookAndFeel);

    pane.setBounds(0, 0, 1000, 600);

    CHECK(first.getBounds() == juce::Rectangle<int>(0, 0, 496, 600));
    CHECK(second.getBounds() == juce::Rectangle<int>(504, 0, 496, 600));
}
