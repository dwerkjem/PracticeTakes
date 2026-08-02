#include <catch2/catch_test_macros.hpp>

#include "application/testcontrol/ApprovedWindowStates.h"
#include "application/testcontrol/TestControlClicker.h"

using namespace testcontrol;

namespace
{
// A root with one nested button, mirroring how the shell nests its title bar
// buttons under the main component.
struct Fixture
{
    juce::Component root;
    juce::Component titleBar;
    juce::TextButton button;

    int clicks = 0;

    Fixture()
    {
        root.setVisible(true);
        titleBar.setVisible(true);
        button.setVisible(true);

        button.setComponentID("tools-button");
        button.onClick = [this] { ++clicks; };

        titleBar.addChildComponent(button);
        root.addChildComponent(titleBar);
    }
};
} // namespace

TEST_CASE("a nested component is found by its id", "[testcontrol][clicker]")
{
    Fixture fixture;

    CHECK(findComponentById(fixture.root, "tools-button") == &fixture.button);
}

TEST_CASE("the root itself can be the match", "[testcontrol][clicker]")
{
    juce::Component root;
    root.setComponentID("root-thing");

    CHECK(findComponentById(root, "root-thing") == &root);
}

TEST_CASE("an unknown id finds nothing", "[testcontrol][clicker]")
{
    Fixture fixture;

    CHECK(findComponentById(fixture.root, "no-such-button") == nullptr);
    CHECK(findComponentById(fixture.root, "") == nullptr);
}

TEST_CASE("clicking runs the object's action", "[testcontrol][clicker]")
{
    // The whole point: the action runs, and nothing synthesises a pointer or a
    // key to make it happen.
    Fixture fixture;

    CHECK(clickComponentById(fixture.root, "tools-button"));
    CHECK(fixture.clicks == 1);
}

TEST_CASE("clicking runs the action synchronously", "[testcontrol][clicker]")
{
    // triggerClick would post through the message queue, leaving the channel
    // able to say "I asked" but never "it happened".
    Fixture fixture;

    CHECK(clickComponentById(fixture.root, "tools-button"));
    CHECK(fixture.clicks == 1);

    CHECK(clickComponentById(fixture.root, "tools-button"));
    CHECK(fixture.clicks == 2);
}

TEST_CASE("clicking an unknown object fails", "[testcontrol][clicker]")
{
    Fixture fixture;

    CHECK_FALSE(clickComponentById(fixture.root, "no-such-button"));
    CHECK(fixture.clicks == 0);
}

TEST_CASE("a hidden object is not clickable", "[testcontrol][clicker]")
{
    Fixture fixture;
    fixture.button.setVisible(false);

    CHECK_FALSE(clickComponentById(fixture.root, "tools-button"));
    CHECK(fixture.clicks == 0);
}

TEST_CASE("an object hidden by its parent is not clickable", "[testcontrol][clicker]")
{
    // This is the hamburger button in a wide window: visible in itself, but
    // inside a container that is not showing. Component::isVisible alone would
    // wrongly call it clickable.
    Fixture fixture;
    fixture.titleBar.setVisible(false);

    CHECK(fixture.button.isVisible());
    CHECK_FALSE(clickComponentById(fixture.root, "tools-button"));
    CHECK(fixture.clicks == 0);
}

TEST_CASE("a disabled object is not clickable", "[testcontrol][clicker]")
{
    Fixture fixture;
    fixture.button.setEnabled(false);

    CHECK_FALSE(clickComponentById(fixture.root, "tools-button"));
    CHECK(fixture.clicks == 0);
}

TEST_CASE("a non-button component is not clickable", "[testcontrol][clicker]")
{
    // An id on something that is not a control would otherwise report a
    // successful click that did nothing at all.
    juce::Component root;
    juce::Component label;

    root.setVisible(true);
    label.setVisible(true);
    label.setComponentID("not-a-button");
    root.addChildComponent(label);

    CHECK_FALSE(clickComponentById(root, "not-a-button"));
}

TEST_CASE("a component outside the tree is not clickable", "[testcontrol][clicker]")
{
    // isEffectivelyVisible walks up to the root it was given; a component
    // reached some other way must not be actionable through this root.
    juce::Component root;
    juce::Component stray;

    root.setVisible(true);
    stray.setVisible(true);

    CHECK_FALSE(isEffectivelyVisible(stray, root));
}

TEST_CASE("every approved click target id is a plausible component id", "[testcontrol][clicker]")
{
    // Guards against an approved id that could never match a component --
    // whitespace or an empty string would silently never resolve.
    for (const ApprovedClickTarget& target : approvedClickTargets())
    {
        INFO("target " << target.id);

        CHECK_FALSE(target.id.empty());
        CHECK(target.id.find(' ') == std::string::npos);
    }
}
