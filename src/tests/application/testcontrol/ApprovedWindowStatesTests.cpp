#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

#include "application/testcontrol/ApprovedWindowStates.h"

using namespace testcontrol;

namespace
{
[[nodiscard]] const ApprovedWindowState& require(const std::string& id)
{
    const ApprovedWindowState* state = findApprovedWindowState(id);
    REQUIRE(state != nullptr);

    return *state;
}
} // namespace

TEST_CASE("an approved state can be looked up by name", "[testcontrol][approved]")
{
    const ApprovedWindowState& state = require("tuner-docked");

    CHECK(state.tools == std::vector<std::string>{"tuner"});
    CHECK(state.presentation == ToolPresentation::docked);
    CHECK_FALSE(state.settingsOpen);
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

TEST_CASE("tools and presentation agree", "[testcontrol][approved]")
{
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);

        if (state.tools.empty())
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
    const std::set<std::string> known(knownToolNames().begin(), knownToolNames().end());

    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        for (const std::string& tool : state.tools)
        {
            INFO("state " << state.id << " tool " << tool);
            CHECK(known.count(tool) == 1);
        }
    }
}

TEST_CASE("a state never names the same tool twice", "[testcontrol][approved]")
{
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);

        std::set<std::string> seen(state.tools.begin(), state.tools.end());

        CHECK(seen.size() == state.tools.size());
    }
}

TEST_CASE("every tool is reachable in at least one state", "[testcontrol][approved]")
{
    // A tool with no approved state cannot be manually verified at all, which
    // is the kind of gap that is invisible until a release.
    for (const std::string& tool : knownToolNames())
    {
        INFO("tool " << tool);

        const auto& states = approvedWindowStates();
        const bool reachable = std::any_of(
            states.begin(), states.end(),
            [&tool](const ApprovedWindowState& state)
            {
                return std::find(state.tools.begin(), state.tools.end(), tool) != state.tools.end();
            });

        CHECK(reachable);
    }
}

TEST_CASE(
    "an arrangement is only claimed when there is something to arrange",
    "[testcontrol][approved]")
{
    // A split or tabbed arrangement of one tool is meaningless, and would make
    // the glue's job ambiguous.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);

        if (state.arrangement == WorkspaceArrangement::single)
        {
            CHECK(state.tools.size() <= 1);
        }
        else
        {
            CHECK(state.tools.size() >= 2);
        }
    }
}

TEST_CASE("multi-tool tools are docked", "[testcontrol][approved]")
{
    // Splitting and tab-sharing are workspace arrangements; floating windows do
    // not participate in either.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);

        if (state.tools.size() >= 2)
        {
            CHECK(state.presentation == ToolPresentation::docked);
        }
    }
}

TEST_CASE("both multi-tool arrangements are reachable", "[testcontrol][approved]")
{
    // Layout::tabbed and WorkspaceTabbedComponent exist in the application but
    // were unreachable by any state before these were added.
    const auto& states = approvedWindowStates();

    for (const WorkspaceArrangement arrangement :
         {WorkspaceArrangement::split, WorkspaceArrangement::tabbed})
    {
        const bool reachable = std::any_of(
            states.begin(), states.end(), [arrangement](const ApprovedWindowState& state)
            { return state.arrangement == arrangement; });

        CHECK(reachable);
    }
}

TEST_CASE("the tabbed state shares one strip between two tools", "[testcontrol][approved]")
{
    const ApprovedWindowState& state = require("two-tools-tabbed");

    CHECK(state.arrangement == WorkspaceArrangement::tabbed);
    CHECK(state.tools.size() == 2);
    CHECK(state.presentation == ToolPresentation::docked);
}

TEST_CASE("all three tools can be open at once", "[testcontrol][approved]")
{
    // Also the only state that puts three live audio consumers on the shared
    // capture path simultaneously.
    const ApprovedWindowState& state = require("all-tools-docked");

    CHECK(state.tools.size() == knownToolNames().size());
    CHECK(state.arrangement == WorkspaceArrangement::split);
}

TEST_CASE("each window geometry is reachable", "[testcontrol][approved]")
{
    const auto& states = approvedWindowStates();

    for (const WindowGeometry geometry :
         {WindowGeometry::normal, WindowGeometry::narrow, WindowGeometry::fullscreen})
    {
        const bool reachable = std::any_of(
            states.begin(), states.end(),
            [geometry](const ApprovedWindowState& state) { return state.geometry == geometry; });

        CHECK(reachable);
    }
}

TEST_CASE(
    "the narrow state opens a tool so there is something to look at",
    "[testcontrol][approved]")
{
    // The point of the narrow state is the collapsed title bar, but an empty
    // workspace behind it would make the other two axes unanswerable.
    const ApprovedWindowState& state = require("narrow-window");

    CHECK(state.geometry == WindowGeometry::narrow);
    CHECK_FALSE(state.tools.empty());
}

TEST_CASE("each microphone condition is reachable", "[testcontrol][approved]")
{
    const auto& states = approvedWindowStates();

    for (const MicrophoneCondition condition :
         {MicrophoneCondition::available, MicrophoneCondition::muted,
          MicrophoneCondition::unavailable})
    {
        const bool reachable = std::any_of(
            states.begin(), states.end(), [condition](const ApprovedWindowState& state)
            { return state.microphone == condition; });

        CHECK(reachable);
    }
}

TEST_CASE("the muted state opens a tool that shows the mute", "[testcontrol][approved]")
{
    const ApprovedWindowState& state = require("microphone-muted");

    CHECK(state.microphone == MicrophoneCondition::muted);
    CHECK_FALSE(state.tools.empty());
}

TEST_CASE("the warning state has no usable input", "[testcontrol][approved]")
{
    // This is the only way to see the microphone warning on a machine whose
    // microphone works, which is every machine a release is cut from.
    const ApprovedWindowState& state = require("microphone-warning");

    CHECK(state.microphone == MicrophoneCondition::unavailable);
}

TEST_CASE("a settings panel is only named when settings are open", "[testcontrol][approved]")
{
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);

        if (!state.settingsPanel.empty())
        {
            CHECK(state.settingsOpen);
        }
    }
}

TEST_CASE("the settings sub-panels are distinct and named", "[testcontrol][approved]")
{
    const ApprovedWindowState& device = require("settings-audio-device");
    const ApprovedWindowState& appearance = require("settings-appearance");

    CHECK(device.settingsPanel == "audio-device");
    CHECK(appearance.settingsPanel == "appearance");
    CHECK(device.settingsPanel != appearance.settingsPanel);

    // The default-panel state stays, so "whatever settings opens on" is still
    // verifiable separately from either specific panel.
    CHECK(require("settings-open").settingsPanel.empty());
}

TEST_CASE("settings and feedback are never opened together", "[testcontrol][approved]")
{
    // Two modal-ish windows at once is not a surface anyone is verifying, and
    // it would make the prompt ambiguous.
    for (const ApprovedWindowState& state : approvedWindowStates())
    {
        INFO("state " << state.id);
        CHECK_FALSE((state.settingsOpen && state.feedbackOpen));
    }
}

TEST_CASE("the empty shell is an approved state", "[testcontrol][approved]")
{
    // Needed so a run can verify the shell itself, and so a state can be reset
    // between surfaces without restarting the application.
    const ApprovedWindowState& empty = require("empty");

    CHECK(empty.tools.empty());
    CHECK(empty.presentation == ToolPresentation::none);
    CHECK(empty.arrangement == WorkspaceArrangement::single);
    CHECK(empty.geometry == WindowGeometry::normal);
    CHECK(empty.microphone == MicrophoneCondition::available);
    CHECK_FALSE(empty.settingsOpen);
    CHECK_FALSE(empty.feedbackOpen);
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

TEST_CASE("the hamburger target has a state that reveals it", "[testcontrol][approved]")
{
    // The collapsed menu only exists below the title bar's width threshold, so
    // without a narrow state the target would be approved but unclickable.
    REQUIRE(findApprovedClickTarget("hamburger-button") != nullptr);

    const auto& states = approvedWindowStates();
    const bool narrowReachable = std::any_of(
        states.begin(), states.end(),
        [](const ApprovedWindowState& state) { return state.geometry == WindowGeometry::narrow; });

    CHECK(narrowReachable);
}

TEST_CASE("both vocabularies are non-empty", "[testcontrol][approved]")
{
    // An empty list would make the channel parse perfectly and drive nothing.
    CHECK_FALSE(approvedWindowStates().empty());
    CHECK_FALSE(approvedClickTargets().empty());
}
