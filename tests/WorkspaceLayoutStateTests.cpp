#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/WorkspaceLayoutState.h"

TEST_CASE("drop targets cover tiled tabbed and floating destinations", "[workspace][layout]")
{
    using Zone = WorkspaceLayoutState::DropZone;
    constexpr int width = 1200;
    constexpr int height = 700;

    CHECK(WorkspaceLayoutState::dropZoneForPosition(20, 350, width, height) == Zone::left);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(1180, 350, width, height) == Zone::right);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(600, 80, width, height) == Zone::top);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(600, 680, width, height) == Zone::bottom);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(600, 350, width, height) == Zone::centre);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(1160, 20, width, height) == Zone::floating);
}

TEST_CASE("invalid drop positions leave the layout unchanged", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    const auto initialLayout = state.layout();
    const auto initialFirst = state.first();

    const auto result = state.applyDrop(
        WorkspaceLayoutState::Tool::spectrogram, WorkspaceLayoutState::DropZone::none, true);

    CHECK(result.destination == WorkspaceLayoutState::Destination::unchanged);
    CHECK(state.layout() == initialLayout);
    CHECK(state.first() == initialFirst);
}

TEST_CASE("edge drops select orientation and ordering", "[workspace][layout]")
{
    using Layout = WorkspaceLayoutState::Layout;
    using Tool = WorkspaceLayoutState::Tool;
    using Zone = WorkspaceLayoutState::DropZone;

    WorkspaceLayoutState state;
    CHECK(state.applyDrop(Tool::spectrogram, Zone::left, true).layoutChanged);
    CHECK(state.layout() == Layout::horizontal);
    CHECK(state.first() == Tool::spectrogram);

    CHECK(state.applyDrop(Tool::tuner, Zone::bottom, true).layoutChanged);
    CHECK(state.layout() == Layout::vertical);
    CHECK(state.first() == Tool::spectrogram);
}

TEST_CASE("centre drops create a tab group and activate the dragged tool", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    const auto result = state.applyDrop(
        WorkspaceLayoutState::Tool::spectrogram, WorkspaceLayoutState::DropZone::centre, true);

    CHECK(result.destination == WorkspaceLayoutState::Destination::docked);
    CHECK(state.layout() == WorkspaceLayoutState::Layout::tabbed);
    CHECK(state.active() == WorkspaceLayoutState::Tool::spectrogram);
}

TEST_CASE("floating drops do not mutate the tiled layout", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.setLayout(WorkspaceLayoutState::Layout::vertical);

    const auto result = state.applyDrop(
        WorkspaceLayoutState::Tool::tuner, WorkspaceLayoutState::DropZone::floating, true);

    CHECK(result.destination == WorkspaceLayoutState::Destination::floating);
    CHECK_FALSE(result.layoutChanged);
    CHECK(state.layout() == WorkspaceLayoutState::Layout::vertical);
}

TEST_CASE("drop target boundaries reject points outside the workspace", "[workspace][layout]")
{
    using Zone = WorkspaceLayoutState::DropZone;

    CHECK(WorkspaceLayoutState::dropZoneForPosition(-1, 10, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(10, -1, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(800, 10, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(10, 600, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(0, 0, 0, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(0, 0, 800, 0) == Zone::none);
}

TEST_CASE("floating target takes precedence over top and right edges", "[workspace][layout]")
{
    using Zone = WorkspaceLayoutState::DropZone;
    constexpr int width = 1200;
    constexpr int height = 700;

    CHECK(WorkspaceLayoutState::dropZoneForPosition(width - 1, 0, width, height) == Zone::floating);
    CHECK(
        WorkspaceLayoutState::dropZoneForPosition(width - 170, 63, width, height) ==
        Zone::floating);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(width - 301, 63, width, height) == Zone::top);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(width - 170, 64, width, height) == Zone::right);
}

TEST_CASE("a lone tool docks without changing the stored split layout", "[workspace][layout]")
{
    using Layout = WorkspaceLayoutState::Layout;
    using Tool = WorkspaceLayoutState::Tool;
    using Zone = WorkspaceLayoutState::DropZone;

    WorkspaceLayoutState state;
    state.setLayout(Layout::vertical);
    state.setActiveTool(Tool::tuner);

    const auto result = state.applyDrop(Tool::spectrogram, Zone::left, false);

    CHECK(result.destination == WorkspaceLayoutState::Destination::docked);
    CHECK_FALSE(result.layoutChanged);
    CHECK(state.layout() == Layout::vertical);
    CHECK(state.active() == Tool::spectrogram);
}

TEST_CASE("repeating an established edge layout is idempotent", "[workspace][layout]")
{
    using Tool = WorkspaceLayoutState::Tool;
    using Zone = WorkspaceLayoutState::DropZone;

    WorkspaceLayoutState state;
    REQUIRE(state.applyDrop(Tool::spectrogram, Zone::left, true).layoutChanged);

    const auto repeated = state.applyDrop(Tool::spectrogram, Zone::left, true);

    CHECK(repeated.destination == WorkspaceLayoutState::Destination::docked);
    CHECK_FALSE(repeated.layoutChanged);
}

TEST_CASE("workspace tool pairing is symmetric", "[workspace][layout]")
{
    using Tool = WorkspaceLayoutState::Tool;

    CHECK(WorkspaceLayoutState::otherTool(Tool::tuner) == Tool::spectrogram);
    CHECK(WorkspaceLayoutState::otherTool(Tool::spectrogram) == Tool::tuner);
}
