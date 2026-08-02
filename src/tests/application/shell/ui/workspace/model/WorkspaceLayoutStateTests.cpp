#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/model/WorkspaceLayoutState.h"

#include <optional>
#include <vector>

namespace
{
using Tool = WorkspaceLayoutState::Tool;
using Zone = WorkspaceLayoutState::DropZone;
using Orientation = WorkspaceLayoutState::Orientation;
using NodeKind = WorkspaceLayoutState::NodeKind;
using Node = WorkspaceLayoutState::Node;

constexpr Tool tuner{0};
constexpr Tool spectrogram{1};
constexpr Tool harmonics{2};
} // namespace

TEST_CASE("drop targets cover tiled tabbed and floating destinations", "[workspace][layout]")
{
    constexpr int width = 1200;
    constexpr int height = 700;

    CHECK(WorkspaceLayoutState::dropZoneForPosition(20, 350, width, height) == Zone::left);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(1180, 350, width, height) == Zone::right);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(600, 80, width, height) == Zone::top);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(600, 680, width, height) == Zone::bottom);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(600, 350, width, height) == Zone::centre);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(1160, 20, width, height) == Zone::floating);
}

TEST_CASE("drop target boundaries reject points outside the workspace", "[workspace][layout]")
{
    CHECK(WorkspaceLayoutState::dropZoneForPosition(-1, 10, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(10, -1, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(800, 10, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(10, 600, 800, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(0, 0, 0, 600) == Zone::none);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(0, 0, 800, 0) == Zone::none);
}

TEST_CASE("floating target takes precedence over top and right edges", "[workspace][layout]")
{
    constexpr int width = 1200;
    constexpr int height = 700;

    CHECK(WorkspaceLayoutState::dropZoneForPosition(width - 1, 0, width, height) == Zone::floating);
    CHECK(
        WorkspaceLayoutState::dropZoneForPosition(width - 170, 63, width, height) ==
        Zone::floating);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(width - 301, 63, width, height) == Zone::top);
    CHECK(WorkspaceLayoutState::dropZoneForPosition(width - 170, 64, width, height) == Zone::right);
}

TEST_CASE("a new tree is empty and contains nothing", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    CHECK(state.empty());
    CHECK_FALSE(state.contains(tuner));
    CHECK(state.rootNode() == nullptr);
}

TEST_CASE("the first tool inserted becomes the sole root pane", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);

    REQUIRE_FALSE(state.empty());
    CHECK(state.contains(tuner));
    REQUIRE(state.rootNode() != nullptr);
    CHECK(state.rootNode()->kind == NodeKind::leaf);
    CHECK(state.rootNode()->tool == tuner);
}

TEST_CASE(
    "left/right edge drops create a horizontal split with the requested order",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::left);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::split);
    CHECK(root->orientation == Orientation::horizontal);
    REQUIRE(root->first != nullptr);
    REQUIRE(root->second != nullptr);
    CHECK(root->first->tool == spectrogram);
    CHECK(root->second->tool == tuner);
}

TEST_CASE(
    "top/bottom edge drops create a vertical split with the requested order",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::bottom);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::split);
    CHECK(root->orientation == Orientation::vertical);
    REQUIRE(root->first != nullptr);
    REQUIRE(root->second != nullptr);
    CHECK(root->first->tool == tuner);
    CHECK(root->second->tool == spectrogram);
}

TEST_CASE("centre drops create a tab group and activate the dropped tool", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::centre);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::tabs);
    CHECK(root->tabs == std::vector<Tool>{tuner, spectrogram});
    CHECK(root->activeTab == spectrogram);
}

TEST_CASE("a pane keeps subdividing fractally as more tools are tiled", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::right);
    // Split whichever pane holds `tuner` again -- the *other* pane
    // (spectrogram) must be left completely untouched by this.
    state.insert(harmonics, tuner, Zone::bottom);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::split);
    CHECK(root->orientation == Orientation::horizontal);
    REQUIRE(root->first != nullptr);
    REQUIRE(root->second != nullptr);

    // spectrogram's pane (root->second, since tuner was pushed left) is
    // untouched: still a plain leaf.
    CHECK(root->second->kind == NodeKind::leaf);
    CHECK(root->second->tool == spectrogram);

    // tuner's pane subdivided again into its own nested vertical split.
    CHECK(root->first->kind == NodeKind::split);
    CHECK(root->first->orientation == Orientation::vertical);
    REQUIRE(root->first->first != nullptr);
    REQUIRE(root->first->second != nullptr);
    CHECK(root->first->first->tool == tuner);
    CHECK(root->first->second->tool == harmonics);

    CHECK(state.contains(tuner));
    CHECK(state.contains(spectrogram));
    CHECK(state.contains(harmonics));
}

TEST_CASE(
    "dropping centre onto a member of a tab group joins the same group",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::centre);
    state.insert(harmonics, spectrogram, Zone::centre);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::tabs);
    CHECK(root->tabs == std::vector<Tool>{tuner, spectrogram, harmonics});
    CHECK(root->activeTab == harmonics);
}

TEST_CASE(
    "tiling a member of a tab group splits the whole group as one unit",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::centre);
    state.insert(harmonics, tuner, Zone::right);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::split);
    REQUIRE(root->first != nullptr);
    REQUIRE(root->second != nullptr);
    CHECK(root->second->kind == NodeKind::leaf);
    CHECK(root->second->tool == harmonics);
    CHECK(root->first->kind == NodeKind::tabs);
    CHECK(root->first->tabs == std::vector<Tool>{tuner, spectrogram});
}

TEST_CASE("dropping a pane onto itself is a no-op", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(tuner, tuner, Zone::left);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::leaf);
    CHECK(root->tool == tuner);
}

TEST_CASE("none and floating zones are not placement requests", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::none);
    CHECK_FALSE(state.contains(spectrogram));

    state.insert(spectrogram, tuner, Zone::floating);
    CHECK_FALSE(state.contains(spectrogram));
}

TEST_CASE(
    "removing a leaf from a two-way split promotes its sibling to root",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::left);

    state.remove(tuner);

    CHECK_FALSE(state.contains(tuner));
    CHECK(state.contains(spectrogram));
    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::leaf);
    CHECK(root->tool == spectrogram);
}

TEST_CASE(
    "removing a leaf deep in a fractal tree only collapses its own level",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::right);
    state.insert(harmonics, tuner, Zone::bottom);

    state.remove(harmonics);

    CHECK_FALSE(state.contains(harmonics));
    CHECK(state.contains(tuner));
    CHECK(state.contains(spectrogram));

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::split);
    REQUIRE(root->first != nullptr);
    REQUIRE(root->second != nullptr);
    // tuner's nested split collapsed back into a plain leaf...
    CHECK(root->first->kind == NodeKind::leaf);
    CHECK(root->first->tool == tuner);
    // ...while spectrogram's untouched sibling pane is unaffected.
    CHECK(root->second->kind == NodeKind::leaf);
    CHECK(root->second->tool == spectrogram);
}

TEST_CASE("removing from a three-way tab group just shrinks it", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::centre);
    state.insert(harmonics, tuner, Zone::centre);

    state.remove(spectrogram);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::tabs);
    CHECK(root->tabs == std::vector<Tool>{tuner, harmonics});
}

TEST_CASE(
    "removing from a two-way tab group collapses it back to a plain leaf",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::centre);

    state.remove(spectrogram);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::leaf);
    CHECK(root->tool == tuner);
}

TEST_CASE("removing the last tool empties the tree", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);

    state.remove(tuner);

    CHECK(state.empty());
    CHECK_FALSE(state.contains(tuner));
}

TEST_CASE(
    "inserting with no findable target merges at the first pane instead of discarding the "
    "rest of the tree",
    "[workspace][layout]")
{
    constexpr Tool extra{3};

    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::right);
    state.insert(harmonics, tuner, Zone::bottom);

    // No target supplied even though the tree already has a real shape --
    // must not blindly replace the root and lose spectrogram/harmonics.
    state.insert(extra, std::nullopt, Zone::centre);

    CHECK(state.contains(tuner));
    CHECK(state.contains(spectrogram));
    CHECK(state.contains(harmonics));
    CHECK(state.contains(extra));
}

TEST_CASE(
    "inserting a tool already in the tree moves it instead of duplicating it",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::centre);
    state.insert(harmonics, tuner, Zone::centre);

    // spectrogram is currently tabbed with tuner/harmonics; move it to a
    // brand new tiled position next to harmonics instead.
    state.insert(spectrogram, harmonics, Zone::bottom);

    CHECK(state.contains(spectrogram));
    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::split);
    REQUIRE(root->first != nullptr);
    REQUIRE(root->second != nullptr);
    // tuner/harmonics remain tabbed together, undisturbed...
    CHECK(root->first->kind == NodeKind::tabs);
    CHECK(root->first->tabs == std::vector<Tool>{tuner, harmonics});
    // ...and spectrogram appears exactly once, at the new split location.
    CHECK(root->second->kind == NodeKind::leaf);
    CHECK(root->second->tool == spectrogram);
}

TEST_CASE(
    "flattening to tabs collapses an arbitrarily deep tree into one tab group",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::right);
    state.insert(harmonics, tuner, Zone::bottom);

    state.applyLayoutCommand(WorkspaceLayoutState::Layout::tabbed);

    CHECK(state.isFlattenedTabs());
    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::tabs);
    CHECK(root->tabs.size() == 3);
    CHECK(state.contains(tuner));
    CHECK(state.contains(spectrogram));
    CHECK(state.contains(harmonics));
}

TEST_CASE("arrange commands change only the root split's orientation", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);
    state.insert(spectrogram, tuner, Zone::left);

    CHECK(state.canSplitRoot());
    CHECK(state.isRootOrientation(Orientation::horizontal));

    state.applyLayoutCommand(WorkspaceLayoutState::Layout::vertical);
    CHECK(state.isRootOrientation(Orientation::vertical));

    state.applyLayoutCommand(WorkspaceLayoutState::Layout::horizontal);
    CHECK(state.isRootOrientation(Orientation::horizontal));
}

TEST_CASE("arrange commands are a no-op when the root isn't a split", "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(tuner, std::nullopt, Zone::centre);

    CHECK_FALSE(state.canSplitRoot());
    state.applyLayoutCommand(WorkspaceLayoutState::Layout::vertical);

    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::leaf);
    CHECK(root->tool == tuner);
}

TEST_CASE(
    "whole-tree replacement installs ratios tabs and unique stable node identities",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    auto replacement = WorkspaceLayoutState::makeSplitNode(
        Orientation::horizontal, 0.63, WorkspaceLayoutState::makeLeafNode(tuner),
        WorkspaceLayoutState::makeTabsNode({spectrogram, harmonics}, harmonics));

    REQUIRE(state.replaceRoot(std::move(replacement)));
    const auto* root = state.rootNode();
    REQUIRE(root != nullptr);
    CHECK(root->kind == NodeKind::split);
    CHECK(root->splitRatio == 0.63);
    REQUIRE(root->first != nullptr);
    REQUIRE(root->second != nullptr);
    CHECK(root->id != 0);
    CHECK(root->first->id != 0);
    CHECK(root->second->id != 0);
    CHECK(root->id != root->first->id);
    CHECK(root->id != root->second->id);
    CHECK(root->first->id != root->second->id);
    CHECK(root->second->activeTab == harmonics);
}

TEST_CASE(
    "whole-tree replacement rejects duplicate tool placement without changing state",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    state.insert(harmonics, std::nullopt, Zone::centre);
    const auto originalId = state.rootNode()->id;

    auto duplicate = WorkspaceLayoutState::makeSplitNode(
        Orientation::vertical, 0.5, WorkspaceLayoutState::makeLeafNode(tuner),
        WorkspaceLayoutState::makeTabsNode({spectrogram, tuner}, spectrogram));

    CHECK_FALSE(state.replaceRoot(std::move(duplicate)));
    REQUIRE(state.rootNode() != nullptr);
    CHECK(state.rootNode()->id == originalId);
    CHECK(state.rootNode()->tool == harmonics);
}

TEST_CASE(
    "split ratios and active tabs mutate through stable node identities",
    "[workspace][layout]")
{
    WorkspaceLayoutState state;
    auto replacement = WorkspaceLayoutState::makeSplitNode(
        Orientation::horizontal, 0.5, WorkspaceLayoutState::makeLeafNode(tuner),
        WorkspaceLayoutState::makeTabsNode({spectrogram, harmonics}, spectrogram));
    REQUIRE(state.replaceRoot(std::move(replacement)));

    const auto splitId = state.rootNode()->id;
    const auto tabsId = state.rootNode()->second->id;
    CHECK(state.setSplitRatio(splitId, 1.4));
    CHECK(state.setActiveTab(tabsId, harmonics));

    REQUIRE(state.rootNode() != nullptr);
    CHECK(state.rootNode()->id == splitId);
    CHECK(state.rootNode()->splitRatio == 0.9);
    REQUIRE(state.rootNode()->second != nullptr);
    CHECK(state.rootNode()->second->id == tabsId);
    CHECK(state.rootNode()->second->activeTab == harmonics);
    CHECK_FALSE(state.setActiveTab(tabsId, tuner));
    CHECK_FALSE(state.setSplitRatio(tabsId, 0.4));
}

TEST_CASE("opening tools the way openTool does produces tabs, not a split", "[workspace][layout]")
{
    // The behaviour that misled the manual GUI harness. openTool inserts every
    // docked tool with DropZone::centre, which auto-tabs alongside whatever is
    // already there -- so "open two tools docked" is never a side-by-side
    // layout, however it is described.
    WorkspaceLayoutState state;

    state.insert(tuner, std::nullopt, WorkspaceLayoutState::DropZone::centre);
    state.insert(spectrogram, std::nullopt, WorkspaceLayoutState::DropZone::centre);

    CHECK(state.isFlattenedTabs());
    CHECK_FALSE(state.canSplitRoot());
}

TEST_CASE("an arrange command cannot turn tabs into a split", "[workspace][layout]")
{
    // applyLayoutCommand only re-orients an *existing* split. Asking for
    // horizontal while the root is a tabs node silently does nothing, which is
    // why the harness reported "there tabbed not side by side".
    WorkspaceLayoutState state;

    state.insert(tuner, std::nullopt, WorkspaceLayoutState::DropZone::centre);
    state.insert(spectrogram, std::nullopt, WorkspaceLayoutState::DropZone::centre);

    state.applyLayoutCommand(WorkspaceLayoutState::Layout::horizontal);

    CHECK(state.isFlattenedTabs());
    CHECK_FALSE(state.canSplitRoot());
}

TEST_CASE("placing a tool beside another does produce a split", "[workspace][layout]")
{
    // The fix: tiling has to be asked for explicitly, by naming the pane to
    // sit beside and the side to sit on.
    WorkspaceLayoutState state;

    state.insert(tuner, std::nullopt, WorkspaceLayoutState::DropZone::centre);
    state.insert(spectrogram, tuner, WorkspaceLayoutState::DropZone::right);

    CHECK_FALSE(state.isFlattenedTabs());
    CHECK(state.canSplitRoot());
    CHECK(state.isRootOrientation(WorkspaceLayoutState::Orientation::horizontal));
}

TEST_CASE("three tools placed beside each other all stay visible", "[workspace][layout]")
{
    // "All three tools open at once" reported "not all visable" for the same
    // reason: they were tabbed, so only one showed.
    WorkspaceLayoutState state;

    state.insert(tuner, std::nullopt, WorkspaceLayoutState::DropZone::centre);
    state.insert(spectrogram, tuner, WorkspaceLayoutState::DropZone::right);
    state.insert(harmonics, spectrogram, WorkspaceLayoutState::DropZone::right);

    CHECK_FALSE(state.isFlattenedTabs());
    CHECK(state.canSplitRoot());
}
