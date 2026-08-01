#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "application/testcontrol/TestControlSession.h"

using namespace testcontrol;

namespace
{
// Records what it was asked to do, and can be told to refuse -- which is the
// case that matters, because a surface that cannot be established must be
// reported rather than silently judged.
struct FakeTarget : TestControlTarget
{
    std::vector<std::string> appliedStates;
    std::vector<std::string> clicked;
    std::vector<std::string> geometries;
    std::string current;
    bool quitRequested = false;

    bool refuseState = false;
    bool refuseClick = false;
    bool refuseGeometry = false;

    bool applyState(const ApprovedWindowState& state) override
    {
        if (refuseState)
        {
            return false;
        }

        appliedStates.push_back(state.id);
        current = state.id;

        return true;
    }

    bool clickTarget(const std::string& id) override
    {
        if (refuseClick)
        {
            return false;
        }

        clicked.push_back(id);

        return true;
    }

    bool applyGeometry(const std::string& geometry) override
    {
        if (refuseGeometry)
        {
            return false;
        }

        geometries.push_back(geometry);

        return true;
    }

    [[nodiscard]] std::string currentStateId() const override
    {
        return current;
    }

    void requestQuit() override
    {
        quitRequested = true;
    }
};
} // namespace

TEST_CASE("opening an approved state applies it", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("open-state tuner-docked");

    CHECK(response.success);
    REQUIRE(target.appliedStates.size() == 1);
    CHECK(target.appliedStates.front() == "tuner-docked");
}

TEST_CASE("opening an unapproved state fails without touching the app", "[testcontrol][session]")
{
    // Closed vocabulary: approximating would let a drifted harness believe it
    // verified a surface that no longer exists.
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("open-state tuner-upside-down");

    CHECK_FALSE(response.success);
    CHECK(response.error.find("tuner-upside-down") != std::string::npos);
    CHECK(target.appliedStates.empty());
}

TEST_CASE("a state that cannot be established is reported", "[testcontrol][session]")
{
    FakeTarget target;
    target.refuseState = true;

    TestControlSession session{target};

    const Response response = session.handleLine("open-state tuner-docked");

    CHECK_FALSE(response.success);
    CHECK_FALSE(response.error.empty());
}

TEST_CASE("clicking an approved object invokes it", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("click tools-button");

    CHECK(response.success);
    REQUIRE(target.clicked.size() == 1);
    CHECK(target.clicked.front() == "tools-button");
}

TEST_CASE("clicking an unapproved object fails without touching the app", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("click launch-missiles");

    CHECK_FALSE(response.success);
    CHECK(target.clicked.empty());
}

TEST_CASE("clicking an object that is not present is a failure", "[testcontrol][session]")
{
    // The hamburger button in a wide window is approved but absent. Reporting
    // success there would mean the harness records a check that never happened.
    FakeTarget target;
    target.refuseClick = true;

    TestControlSession session{target};

    const Response response = session.handleLine("click hamburger-button");

    CHECK_FALSE(response.success);
    CHECK(response.error.find("hamburger-button") != std::string::npos);
}

TEST_CASE("list-states reports every approved state", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("list-states");

    CHECK(response.success);
    CHECK(response.items.size() == approvedWindowStates().size());
}

TEST_CASE("list-objects reports every approved object", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("list-objects");

    CHECK(response.success);
    CHECK(response.items.size() == approvedClickTargets().size());
}

TEST_CASE("status reports the current state", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    CHECK(session.handleLine("open-state spectrogram-docked").success);

    const Response response = session.handleLine("status");

    CHECK(response.success);
    REQUIRE(response.items.size() == 1);
    CHECK(response.items.front() == "spectrogram-docked");
}

TEST_CASE("status says none rather than inventing a state", "[testcontrol][session]")
{
    // True at startup and after a click has changed things. Saying so plainly
    // beats guessing at a state name.
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("status");

    CHECK(response.success);
    REQUIRE(response.items.size() == 1);
    CHECK(response.items.front() == "none");
}

TEST_CASE("quit requests shutdown and finishes the session", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    CHECK_FALSE(session.isFinished());

    const Response response = session.handleLine("quit");

    CHECK(response.success);
    CHECK(target.quitRequested);
    CHECK(session.isFinished());
}

TEST_CASE("a blank line succeeds and does nothing", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("   ");

    CHECK(response.success);
    CHECK(response.items.empty());
    CHECK(target.appliedStates.empty());
    CHECK(target.clicked.empty());
}

TEST_CASE("a malformed line reports the parse error", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("open-state");

    CHECK_FALSE(response.success);
    CHECK_FALSE(response.error.empty());
}

TEST_CASE("a failure always carries a reason", "[testcontrol][session]")
{
    // A failure with no reason would leave the harness unable to record why a
    // surface was not verified.
    FakeTarget target;
    target.refuseState = true;
    target.refuseClick = true;

    TestControlSession session{target};

    for (const char* line :
         {"nonsense", "open-state", "open-state no-such-state", "click no-such-object",
          "open-state tuner-docked", "click tools-button"})
    {
        INFO("line '" << line << "'");

        const Response response = session.handleLine(line);

        REQUIRE_FALSE(response.success);
        CHECK_FALSE(response.error.empty());
    }
}

TEST_CASE("a successful reply renders items then ok", "[testcontrol][session]")
{
    Response response;
    response.success = true;
    response.items = {"first", "second"};

    CHECK(renderResponse(response) == "item first\nitem second\nok\n");
}

TEST_CASE("an empty success renders just ok", "[testcontrol][session]")
{
    Response response;
    response.success = true;

    CHECK(renderResponse(response) == "ok\n");
}

TEST_CASE("a failure renders its reason", "[testcontrol][session]")
{
    Response response;
    response.success = false;
    response.error = "no approved state 'x'";

    CHECK(renderResponse(response) == "error no approved state 'x'\n");
}

TEST_CASE("every reply ends with a verdict line", "[testcontrol][session]")
{
    // The transport is a pipe, so the reader needs to know a reply is complete
    // without counting bytes or waiting for a timeout.
    FakeTarget target;
    TestControlSession session{target};

    for (const char* line : {"list-states", "status", "open-state tuner-docked", "nonsense", ""})
    {
        INFO("line '" << line << "'");

        const std::string rendered = renderResponse(session.handleLine(line));

        REQUIRE_FALSE(rendered.empty());
        CHECK(rendered.back() == '\n');

        const bool endsWithVerdict =
            rendered.find("\nok\n") != std::string::npos || rendered.rfind("ok\n", 0) == 0 ||
            rendered.find("error ") != std::string::npos;

        CHECK(endsWithVerdict);
    }
}

TEST_CASE("an approved geometry is applied", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("geometry narrow");

    CHECK(response.success);
    REQUIRE(target.geometries.size() == 1);
    CHECK(target.geometries.front() == "narrow");
}

TEST_CASE("an unapproved geometry is refused without touching the app", "[testcontrol][session]")
{
    FakeTarget target;
    TestControlSession session{target};

    const Response response = session.handleLine("geometry enormous");

    CHECK_FALSE(response.success);
    CHECK(target.geometries.empty());
}

TEST_CASE("a geometry that cannot be applied is reported", "[testcontrol][session]")
{
    FakeTarget target;
    target.refuseGeometry = true;

    TestControlSession session{target};

    const Response response = session.handleLine("geometry narrow");

    CHECK_FALSE(response.success);
    CHECK_FALSE(response.error.empty());
}

TEST_CASE("every approved geometry name is accepted", "[testcontrol][session]")
{
    // The harness maps its sweep names onto these, so a rename here breaks the
    // sweep silently unless something pins them.
    FakeTarget target;
    TestControlSession session{target};

    for (const std::string& name : approvedGeometryNames())
    {
        INFO("geometry " << name);
        CHECK(session.handleLine("geometry " + name).success);
    }
}
