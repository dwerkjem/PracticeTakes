#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/workspace/WorkspaceToolState.h"

TEST_CASE("dock and float transitions retain the live tool instance", "[workspace]")
{
    WorkspaceToolState state;

    const auto opened = state.present(WorkspaceToolState::Presentation::docked);
    REQUIRE(opened.requiresNewInstance);
    CHECK(state.instanceGeneration() == 1);

    const auto floated = state.present(WorkspaceToolState::Presentation::floating);
    CHECK(floated.presentationChanged);
    CHECK_FALSE(floated.requiresNewInstance);
    CHECK(state.instanceGeneration() == 1);

    const auto docked = state.present(WorkspaceToolState::Presentation::docked);
    CHECK(docked.presentationChanged);
    CHECK_FALSE(docked.requiresNewInstance);
    CHECK(state.instanceGeneration() == 1);
}

TEST_CASE("close and reopen creates one replacement tool instance", "[workspace]")
{
    WorkspaceToolState state;
    static_cast<void>(state.present(WorkspaceToolState::Presentation::floating));

    const auto closed = state.close();
    REQUIRE(closed.presentationChanged);
    CHECK_FALSE(state.isOpen());

    const auto reopened = state.present(WorkspaceToolState::Presentation::docked);
    CHECK(reopened.requiresNewInstance);
    CHECK(state.instanceGeneration() == 2);
}

TEST_CASE("requesting the current presentation is idempotent", "[workspace]")
{
    WorkspaceToolState state;
    static_cast<void>(state.present(WorkspaceToolState::Presentation::docked));

    const auto unchanged = state.present(WorkspaceToolState::Presentation::docked);
    CHECK_FALSE(unchanged.presentationChanged);
    CHECK_FALSE(unchanged.requiresNewInstance);
    CHECK(state.instanceGeneration() == 1);
}

TEST_CASE("new workspace tools begin closed without an instance", "[workspace]")
{
    WorkspaceToolState state;

    CHECK(state.presentation() == WorkspaceToolState::Presentation::closed);
    CHECK_FALSE(state.isOpen());
    CHECK(state.instanceGeneration() == 0);
}

TEST_CASE("closing an already closed tool is idempotent", "[workspace]")
{
    WorkspaceToolState state;

    const auto transition = state.close();

    CHECK_FALSE(transition.presentationChanged);
    CHECK_FALSE(transition.requiresNewInstance);
    CHECK(state.instanceGeneration() == 0);
}

TEST_CASE("presenting closed delegates to the close transition", "[workspace]")
{
    WorkspaceToolState state;
    static_cast<void>(state.present(WorkspaceToolState::Presentation::docked));

    const auto transition = state.present(WorkspaceToolState::Presentation::closed);

    CHECK(transition.presentationChanged);
    CHECK_FALSE(transition.requiresNewInstance);
    CHECK_FALSE(state.isOpen());
    CHECK(state.instanceGeneration() == 1);
}

TEST_CASE("each reopen advances the logical instance generation once", "[workspace]")
{
    WorkspaceToolState state;

    for (std::uint64_t expectedGeneration = 1; expectedGeneration <= 3; ++expectedGeneration)
    {
        const auto opened = state.present(WorkspaceToolState::Presentation::floating);
        CHECK(opened.requiresNewInstance);
        CHECK(state.instanceGeneration() == expectedGeneration);
        static_cast<void>(state.close());
    }
}
