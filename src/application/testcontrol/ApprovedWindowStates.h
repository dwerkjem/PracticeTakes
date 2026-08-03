#pragma once

#include <string>
#include <vector>

// The closed vocabulary the test control channel accepts (no JUCE dependency,
// so it is unit testable without a display).
//
// "Approved" is the operative word. The harness cannot ask the application to
// enter an arbitrary state or to click an arbitrary thing -- it can only name
// something from these lists. That gives three properties worth having:
//
//  - A harness that has drifted from the application fails loudly on an
//    unknown name, instead of silently verifying a surface that no longer
//    exists.
//  - The set of surfaces under manual verification is a reviewable list in one
//    file, rather than an emergent property of harness code.
//  - Nothing is addressed by screen position, so a layout change cannot
//    silently redirect a click to the wrong object.
//
// A state describes *what the application should look like*, not the steps to
// get there. The glue decides how to establish it, which is what keeps these
// definitions stable when the route to a surface changes.
namespace testcontrol
{
enum class ToolPresentation
{
    // The state opens no tool.
    none,
    docked,
    floating
};

// How several docked tools sit relative to each other. Only meaningful when a
// state opens more than one.
enum class WorkspaceArrangement
{
    // Zero or one tool; nothing to arrange.
    single,

    // Tiled side by side in the workspace's split tree.
    split,

    // Sharing one tab strip.
    tabbed
};

enum class WindowGeometry
{
    normal,

    // Narrower than the 900px threshold at which MainTitleBar swaps its
    // buttons for the collapsed hamburger menu.
    narrow,

    fullscreen
};

// The audio input condition the state presents.
//
// `unavailable` is established by the control channel telling the audio service
// to report no usable device -- it is application state being set, not input
// being synthesised, and it is the only way to see the microphone warning on a
// machine whose microphone works.
enum class MicrophoneCondition
{
    available,
    muted,
    unavailable
};

// A window configuration the application will put itself into on request.
struct ApprovedWindowState
{
    // Wire name, kebab-case, used by `open-state`.
    std::string id;

    // Which tools the state opens, by tool id or by any historical alias of
    // one -- "harmonics" for the harmonic analyzer, say. ToolCatalog resolves
    // them, and the tests check every name here resolves.
    std::vector<std::string> tools;

    ToolPresentation presentation = ToolPresentation::none;
    WorkspaceArrangement arrangement = WorkspaceArrangement::single;
    WindowGeometry geometry = WindowGeometry::normal;
    MicrophoneCondition microphone = MicrophoneCondition::available;

    bool settingsOpen = false;

    // Which settings panel to show. Empty means whichever the window opens on
    // by default.
    std::string settingsPanel;

    bool feedbackOpen = false;

    // What a tester is looking at in this state. Shown by `list-states` and
    // used by the harness as the prompt heading.
    std::string description;
};

// An object that can be asked to act, as though clicked.
//
// The action is invoked on the object itself -- no pointer is moved and no
// button event is synthesised. That is the whole point: a click here means
// "this control's action ran", not "something happened at these coordinates".
struct ApprovedClickTarget
{
    // Wire name, used by `click`. Matches the component id set on the object.
    std::string id;

    std::string description;
};

// Every state the application will enter on request.
[[nodiscard]] const std::vector<ApprovedWindowState>& approvedWindowStates();

// Every object the application will let be clicked.
[[nodiscard]] const std::vector<ApprovedClickTarget>& approvedClickTargets();

// Look up by wire name. Null when the name is not approved, which callers must
// treat as an error rather than a miss.
[[nodiscard]] const ApprovedWindowState* findApprovedWindowState(const std::string& id);
[[nodiscard]] const ApprovedClickTarget* findApprovedClickTarget(const std::string& id);

// Named themes the `theme` command accepts.
//
// A capture dimension rather than a field on each state, for the same reason
// geometry is: a state describes what is on screen, and every one of them can be
// looked at in either palette. Baking the theme into the state list would double
// it and would couple "which surfaces exist" to "which palettes exist".
[[nodiscard]] const std::vector<std::string>& approvedThemeNames();

// Named geometries the `geometry` command accepts. A closed vocabulary for the
// same reason states are: an unrecognised name is an error, not a guess.
//
// Deliberately applied by the application resizing itself. The window
// advertises a 980px minimum width, which a window manager honours, and that is
// above the 900px threshold at which the title bar collapses -- so an external
// resize can never reach the collapsed menu.
[[nodiscard]] const std::vector<std::string>& approvedGeometryNames();

} // namespace testcontrol
