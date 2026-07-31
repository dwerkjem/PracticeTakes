#pragma once

#include <string>

// Parsing for the development-only test control channel (no JUCE dependency, so
// it is unit testable without a display).
//
// The manual GUI harness drives the application through this channel. Two rules
// shape the whole design, and both come from the same place -- synthesising
// input is not allowed:
//
//  1. The harness never fakes a mouse or a keyboard. It asks the application to
//     put itself into a named, *approved* state, and it asks named objects to
//     act. Nothing is addressed by screen coordinate, so nothing breaks when a
//     layout changes -- which is what makes the harness survive UI work.
//  2. The vocabulary is closed. A state or object the application does not
//     recognise is an error, never a best-effort guess, so a harness that has
//     drifted from the application fails loudly instead of appearing to work.
//
// This is a machine interface, so parsing is strict: an unknown verb, a missing
// argument, or a trailing extra argument is rejected rather than tolerated.
namespace testcontrol
{
enum class CommandKind
{
    // Blank or whitespace-only input. Not an error -- a line-oriented reader
    // will see these and should skip them.
    blank,

    // `open-state <id>` -- put the application into an approved window state.
    openState,

    // `click <id>` -- ask an approved object to perform its action, as though
    // it had been clicked. No pointer is moved and no button event is
    // synthesised; the object's own action is invoked directly.
    click,

    // `list-states` / `list-objects` -- discovery, so the harness can check the
    // vocabulary it expects still exists before relying on it.
    listStates,
    listObjects,

    // `status` -- report the current state, for asserting that `open-state`
    // actually took effect.
    status,

    // `quit` -- ask the application to close, so shutdown is exercised through
    // the same channel rather than by killing the process.
    quit,

    // Malformed input. `error` says why.
    invalid
};

struct Command
{
    CommandKind kind = CommandKind::invalid;

    // The state id or object id, for `openState` and `click`. Empty otherwise.
    std::string argument;

    // Human-readable reason, set only when `kind` is `invalid`.
    std::string error;
};

// Parse one line of the control channel.
//
// Surrounding whitespace is tolerated because the line arrives over a pipe;
// everything else is not.
[[nodiscard]] Command parseCommand(const std::string& line);

// The verb for a kind, as it appears on the wire. Returns an empty string for
// `blank` and `invalid`, which have no wire form.
[[nodiscard]] std::string commandVerb(CommandKind kind);
} // namespace testcontrol
