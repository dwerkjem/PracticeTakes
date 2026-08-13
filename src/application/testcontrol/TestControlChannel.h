#pragma once

#include <functional>
#include <string>

#include <juce_events/juce_events.h>

#include "TestControlSession.h"
#include "TestControlTarget.h"

// The transport for the test control channel: newline-delimited commands on
// stdin, newline-delimited replies on stdout.
//
// stdin rather than a socket or a port, because the harness launches the
// application as a child process. There is nothing to allocate, nothing to
// clean up, no port to collide, and the channel dies with the process — none of
// which is true of a socket.
namespace testcontrol
{
// A target assembled from callables, so the shell does not have to inherit an
// interface and this header does not have to know about MainComponent.
class FunctionTestControlTarget final : public TestControlTarget
{
  public:
    std::function<bool(const ApprovedWindowState&)> onApplyState;
    std::function<bool(const std::string&)> onClick;
    std::function<bool(const std::string&)> onGeometry;
    std::function<bool(const std::string&)> onTheme;
    std::function<std::string()> onCurrentStateId;
    std::function<bool()> onHasInput;
    std::function<void()> onQuit;

    bool applyState(const ApprovedWindowState& state) override
    {
        return onApplyState && onApplyState(state);
    }

    bool clickTarget(const std::string& id) override
    {
        return onClick && onClick(id);
    }

    bool applyGeometry(const std::string& geometry) override
    {
        return onGeometry && onGeometry(geometry);
    }

    bool applyTheme(const std::string& theme) override
    {
        return onTheme && onTheme(theme);
    }

    [[nodiscard]] std::string currentStateId() const override
    {
        return onCurrentStateId ? onCurrentStateId() : std::string{};
    }

    [[nodiscard]] bool hasInput() const override
    {
        return onHasInput && onHasInput();
    }

    void requestQuit() override
    {
        if (onQuit)
        {
            onQuit();
        }
    }
};

// Reads commands on a background thread and runs each one on the message
// thread, because everything a command touches is UI state.
//
// Each command is fully handled, and its reply written, before the next line is
// read. The harness can therefore treat a reply as meaning the request has
// finished, rather than that it has been queued — which is what lets it move on
// to asking a human about the surface.
class TestControlChannel final : private juce::Thread
{
  public:
    explicit TestControlChannel(TestControlTarget& target);
    ~TestControlChannel() override;

  private:
    void run() override;

    TestControlTarget& target_;
};
} // namespace testcontrol
