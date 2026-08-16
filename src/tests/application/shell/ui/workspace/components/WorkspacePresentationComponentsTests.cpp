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
