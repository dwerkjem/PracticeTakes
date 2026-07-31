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