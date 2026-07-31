#include <catch2/catch_test_macros.hpp>

#include <set>

#include "application/testcontrol/ApprovedWindowStates.h"

using namespace testcontrol;

TEST_CASE("an approved state can be looked up by name", "[testcontrol][approved]")
{
    const ApprovedWindowState* state = findApprovedWindowState("tuner-docked");

    REQUIRE(state != nullptr);
    CHECK(state->tool == "tuner");
    CHECK(state->presentation == ToolPresentation::docked);
    CHECK_FALSE(state->settingsOpen);
}

TEST_CASE("an unapproved state is not found", "[testcontrol][approved]")
{
    // The harness cannot compose a state of its own; anything not on the list
    // must fail loudly rather than be approximated.
    CHECK(findApprovedWindowState("tuner-tabbed-upside-down") == nullptr);
    CHECK(findApprovedWindowState("") == nullptr);
    CHECK(findApprovedWindowState("TUNER-DOCKED") == nullptr);
}

TEST_CASE("state ids are unique", "[testcontrol][approved]")
{
    // A duplicate id would make one of the two states unreachable, and the
    // harness would silently verify the wrong surface.
    std::set<std::string> seen;

    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);
        CHECK(seen.insert(state.id).second);
    }
}

TEST_CASE("every state is described and named", "[testcontrol][approved]")
{
    // The description is the prompt heading the tester reads, so an empty one
    // is a surface nobody can be asked about meaningfully.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);
        CHECK_FALSE(state.id.empty());
        CHECK_FALSE(state.description.empty());
    }
}

TEST_CASE("a state naming a tool says how it is presented", "[testcontrol][approved]")
{
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);

        if (state.tool.empty())
        {
            CHECK(state.presentation == ToolPresentation::none);
        }
        else
        {
            CHECK(state.presentation != ToolPresentation::none);
        }
    }
}

TEST_CASE("states only name tools the application has", "[testcontrol][approved]")
{
    // ToolType lives in a JUCE header so it cannot be referenced here, which
    // means this list could drift from it. Pinning the names is what catches
    // that.
    const std::set<std::string> known{"tuner", "spectrogram", "harmonics"};

    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);

        if (!state.tool.empty())
        {
            CHECK(known.count(state.tool) == 1);
        }
    }
}

TEST_CASE("every tool is reachable in at least one state", "[testcontrol][approved]")
{
    // A tool with no approved state cannot be manually verified at all, which
    // is the kind of gap that is invisible until a release.
    for (const std::string tool : {"tuner", "spectrogram", "harmonics"})
    {
        INFO("tool " << tool);

        const auto& states = approvedWindowStates();
        const bool reachable = std::any_of(
            states.begin(), states.end(),
            [&tool](const ApprovedWindowState& state) { return state.tool == tool; });

        CHECK(reachable);
    }
}

TEST_CASE("the empty shell is an approved state", "[testcontrol][approved]")
{
    // Needed so a run can verify the shell itself, and so a state can be reset
    // between surfaces without restarting the application.
    const ApprovedWindowState* empty = findApprovedWindowState("empty");

    REQUIRE(empty != nullptr);
    CHECK(empty->tool.empty());
    CHECK(empty->presentation == ToolPresentation::none);
    CHECK_FALSE(empty->settingsOpen);
    CHECK_FALSE(empty->feedbackOpen);
}

TEST_CASE("an approved click target can be looked up by name", "[testcontrol][approved]")
{
    const ApprovedClickTarget* target = findApprovedClickTarget("tools-button");

    REQUIRE(target != nullptr);
    CHECK_FALSE(target->description.empty());
}

TEST_CASE("an unapproved click target is not found", "[testcontrol][approved]")
{
    CHECK(findApprovedClickTarget("some-other-button") == nullptr);
    CHECK(findApprovedClickTarget("") == nullptr);
}

TEST_CASE("click target ids are unique and described", "[testcontrol][approved]")
{
    std::set<std::string> seen;

    for (const ApprovedClickTarget& target : approvedClickTargets())
    {
        INFO("target " << target.id);
        CHECK(seen.insert(target.id).second);
        CHECK_FALSE(target.id.empty());
        CHECK_FALSE(target.description.empty());
    }
}

TEST_CASE("both vocabularies are non-empty", "[testcontrol][approved]")
{
    // An empty list would make the channel parse perfectly and drive nothing.
    CHECK_FALSE(approvedWindowStates().empty());
    CHECK_FALSE(approvedClickTargets().empty());
}
