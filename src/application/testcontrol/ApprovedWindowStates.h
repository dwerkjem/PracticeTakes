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
namespace testcontrol
{
enum class ToolPresentation
{
    // The state opens no tool.
    none,
    docked,
    floating
};

// A window configuration the application will put itself into on request.
struct ApprovedWindowState
{
    // Wire name, kebab-case, used by `open-state`.
    std::string id;

    // Which tool the state opens: "tuner", "spectrogram", "harmonics", or
    // empty for a state that opens none. A string rather than the application's
    // ToolType enum, because that enum lives in a JUCE header and this list has
    // to stay testable without one; the glue maps it.
    std::string tool;

    ToolPresentation presentation = ToolPresentation::none;

    bool settingsOpen = false;
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
} // namespace testcontrol
