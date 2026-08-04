#pragma once

#include <string>

#include "ApprovedWindowStates.h"

// What the test control channel needs the application to be able to do.
//
// An interface rather than a direct dependency on MainComponent, for the reason
// the repository already splits logic out of its Component classes: it lets the
// whole dispatch layer be tested against a fake, with no display, no audio
// device, and no JUCE message loop. The real implementation is the shell; the
// test implementation is a struct that records what it was asked to do.
namespace testcontrol
{
class TestControlTarget
{
  public:
    virtual ~TestControlTarget() = default;

    // Put the application into `state`. Returns false when the state is
    // recognised but could not be established -- a tool that refused to open,
    // a settings panel that no longer exists -- which the channel reports as a
    // failure rather than pretending the surface is ready to be judged.
    [[nodiscard]] virtual bool applyState(const ApprovedWindowState& state) = 0;

    // Invoke the named object's action, as though it had been clicked. No
    // pointer is moved and no button event is synthesised.
    //
    // Returns false when the object is approved but not currently present --
    // the hamburger button in a wide window, say -- which is a real failure and
    // not something to paper over.
    [[nodiscard]] virtual bool clickTarget(const std::string& id) = 0;

    // Resize the window to a named geometry. Returns false when the name is
    // approved but could not be applied.
    [[nodiscard]] virtual bool applyGeometry(const std::string& geometry) = 0;

    // Switch the application's palette. Returns false when the name is not one
    // the application knows how to apply.
    [[nodiscard]] virtual bool applyTheme(const std::string& theme) = 0;

    // The id of the state the application is currently in, or empty if it is
    // not in an approved state (at startup, or after a click changed things).
    [[nodiscard]] virtual std::string currentStateId() const = 0;

    // Begin an orderly shutdown, so the harness exercises the real quit path
    // rather than killing the process.
    virtual void requestQuit() = 0;
};
} // namespace testcontrol
