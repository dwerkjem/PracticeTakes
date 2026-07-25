#include <catch2/catch_test_macros.hpp>

#include "app/WorkspaceToolState.h"

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
