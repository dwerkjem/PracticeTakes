#pragma once

#include <string>
#include <vector>

#include "TestControlCommand.h"
#include "TestControlTarget.h"

// The whole dispatch layer of the test control channel, with no JUCE and no I/O
// (so it is unit testable against a fake target, without a display).
//
// A session turns one line from the harness into one response. Everything that
// could go wrong -- an unknown verb, an unapproved state, an object that is not
// currently present -- is an explicit failure response rather than silence,
// because the harness has to be able to tell "I verified this surface" apart
// from "I asked for something and nothing happened".
namespace testcontrol
{
// The reply to one command.
//
// Line-oriented because the transport is a pipe: `item` lines carry payload,
// and the final line is the verdict, so a reader knows when a reply is complete
// without counting bytes or waiting for a timeout.
struct Response
{
    bool success = false;

    // Payload, one entry per line, sent before the verdict. Empty for commands
    // that only succeed or fail.
    std::vector<std::string> items;

    // Set only on failure, and always non-empty when `success` is false, so a
    // failure can never be reported without a reason.
    std::string error;
};

// Render a response to the wire: each item as `item <text>`, then `ok` or
// `error <reason>`.
[[nodiscard]] std::string renderResponse(const Response& response);

class TestControlSession
{
  public:
    explicit TestControlSession(TestControlTarget& target) : target_(target) {}

    // Handle one parsed command.
    [[nodiscard]] Response handle(const Command& command);

    // Parse and handle one raw line. Blank lines produce a response that is
    // successful and empty, so a reader can send them harmlessly.
    [[nodiscard]] Response handleLine(const std::string& line);

    // Whether `quit` has been seen. The reader loop stops on this rather than
    // on the pipe closing, so shutdown is deterministic.
    [[nodiscard]] bool isFinished() const noexcept
    {
        return finished_;
    }

  private:
    TestControlTarget& target_;
    bool finished_ = false;
};
} // namespace testcontrol
