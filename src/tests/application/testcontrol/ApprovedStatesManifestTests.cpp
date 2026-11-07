#include <catch2/catch_test_macros.hpp>

#include "application/shell/ui/ShellWindowTitles.h"
#include "application/testcontrol/ApprovedStatesManifest.h"

#include <juce_core/juce_core.h>

#include <string>

// The manifest the capture harness reads instead of restating the approved
// states. See ApprovedStatesManifest.h.
using namespace testcontrol;

namespace
{
[[nodiscard]] juce::File committedManifest()
{
    return juce::File(PRACTICE_TAKES_SOURCE_DIR).getChildFile(approvedStatesManifestPath());
}

// What the file holds, with Windows line endings taken back out: a checkout
// with autocrlf converts them, and that is not a stale manifest.
[[nodiscard]] std::string committedText()
{
    return committedManifest().loadFileAsString().replace("\r\n", "\n").toStdString();
}

[[nodiscard]] const ApprovedWindowState& require(const std::string& id)
{
    const ApprovedWindowState* state = findApprovedWindowState(id);
    REQUIRE(state != nullptr);

    return *state;
}
} // namespace

TEST_CASE("the committed approved-states manifest is up to date", "[testcontrol][manifest]")
{
    // The harness reads the committed file, not this function, so a state
    // added, removed or changed without regenerating would leave the harness
    // planning captures of a list the application no longer has.
    INFO(
        "A local build of PracticeTakesTests regenerates it; commit the result. Or run: "
        "cmake --build build --target approved-states-manifest  ("
        << approvedStatesManifestPath() << ")");

    REQUIRE(committedManifest().existsAsFile());
    CHECK(committedText() == approvedStatesManifestJson());
}

TEST_CASE("the manifest is JSON the harness can read", "[testcontrol][manifest]")
{
    const juce::var manifest = juce::JSON::parse(juce::String(approvedStatesManifestJson()));
    REQUIRE(manifest.getDynamicObject() != nullptr);

    const juce::Array<juce::var>* states = manifest["states"].getArray();
    REQUIRE(states != nullptr);
    CHECK(static_cast<std::size_t>(states->size()) == approvedWindowStates().size());

    for (int i = 0; i < states->size(); ++i)
    {
        const ApprovedWindowState& expected = approvedWindowStates()[static_cast<std::size_t>(i)];
        const juce::var& state = states->getReference(i);
        INFO(expected.id);

        CHECK(state["id"].toString().toStdString() == expected.id);
        CHECK(state["description"].toString().toStdString() == expected.description);
        CHECK(state["tools"].getArray()->size() == static_cast<int>(expected.tools.size()));
        CHECK(static_cast<double>(state["toneHz"]) == expected.toneHz);
    }

    CHECK(
        manifest["geometries"].getArray()->size() ==
        static_cast<int>(approvedGeometryNames().size()));
    CHECK(manifest["themes"].getArray()->size() == static_cast<int>(approvedThemeNames().size()));
    CHECK(
        manifest["clickTargets"].getArray()->size() ==
        static_cast<int>(approvedClickTargets().size()));
}

TEST_CASE("the manifest writes the same bytes every time", "[testcontrol][manifest]")
{
    CHECK(approvedStatesManifestJson() == approvedStatesManifestJson());
}

TEST_CASE("each state names the window it is a picture of", "[testcontrol][manifest]")
{
    CHECK(windowTitleOf(require("tuner-docked")).empty());
    CHECK(windowTitleOf(require("empty")).empty());
    CHECK(windowTitleOf(require("settings-practice")) == shellwindows::settings);
    CHECK(windowTitleOf(require("feedback-open")) == shellwindows::feedback);

    // Over a docked tool, the form is still the window on top.
    CHECK(windowTitleOf(require("feedback-over-tool")) == shellwindows::feedback);

    CHECK(windowTitleOf(require("score-import-clean")) == shellwindows::scoreImport);
    CHECK(windowTitleOf(require("score-import-in-progress")) == shellwindows::scoreImport);

    // A score opened into a tool suppresses the summary, so the picture is of
    // the main window.
    CHECK(windowTitleOf(require("score-view-sample")).empty());

    // A floating tool's window carries its display name, including when the
    // state names the tool by an alias.
    CHECK(windowTitleOf(require("tuner-floating")) == "Tuner");
    CHECK(windowTitleOf(require("harmonics-floating")) == "Harmonic Analyzer");
    CHECK(windowTitleOf(require("score-view-notation-large")) == "Score View");
}

TEST_CASE("Write the approved-states manifest", "[.write-approved-states]")
{
    // Hidden: run after each local build of this binary, and by the
    // approved-states-manifest target. Writes only when the text changed, so
    // an unchanged list leaves the file's timestamp alone.
    const std::string generated = approvedStatesManifestJson();
    if (committedManifest().existsAsFile() && committedText() == generated)
    {
        SUCCEED("Already up to date.");
        return;
    }

    REQUIRE(committedManifest().getParentDirectory().createDirectory());
    REQUIRE(committedManifest().replaceWithText(juce::String(generated), false, false, "\n"));
}
