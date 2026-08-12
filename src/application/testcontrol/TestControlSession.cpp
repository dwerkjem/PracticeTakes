#include "TestControlSession.h"

#include <algorithm>

namespace testcontrol
{
namespace
{
[[nodiscard]] Response succeeded(std::vector<std::string> items = {})
{
    Response response;
    response.success = true;
    response.items = std::move(items);

    return response;
}

[[nodiscard]] Response failed(std::string reason)
{
    Response response;
    response.success = false;
    response.error = std::move(reason);

    return response;
}
} // namespace

std::string renderResponse(const Response& response)
{
    std::string rendered;

    for (const std::string& item : response.items)
    {
        rendered += "item " + item + "\n";
    }

    rendered += response.success ? "ok\n" : "error " + response.error + "\n";

    return rendered;
}

Response TestControlSession::handle(const Command& command)
{
    switch (command.kind)
    {
    case CommandKind::blank:
        return succeeded();

    case CommandKind::invalid:
        return failed(command.error);

    case CommandKind::openState:
    {
        const ApprovedWindowState* state = findApprovedWindowState(command.argument);

        if (state == nullptr)
        {
            // Closed vocabulary. Approximating an unapproved state would let a
            // drifted harness believe it verified a surface that no longer
            // exists.
            return failed("no approved state '" + command.argument + "'");
        }

        if (!target_.applyState(*state))
        {
            return failed("could not establish state '" + command.argument + "'");
        }

        return succeeded();
    }

    case CommandKind::click:
    {
        if (findApprovedClickTarget(command.argument) == nullptr)
        {
            return failed("no approved object '" + command.argument + "'");
        }

        if (!target_.clickTarget(command.argument))
        {
            // Approved but not present -- the hamburger button in a wide
            // window, for instance. A real failure, not something to ignore.
            return failed("object '" + command.argument + "' is not available right now");
        }

        return succeeded();
    }

    case CommandKind::geometry:
    {
        const auto& names = approvedGeometryNames();

        if (std::find(names.begin(), names.end(), command.argument) == names.end())
        {
            return failed("no approved geometry '" + command.argument + "'");
        }

        if (!target_.applyGeometry(command.argument))
        {
            return failed("could not apply geometry '" + command.argument + "'");
        }

        return succeeded();
    }

    case CommandKind::theme:
    {
        const auto& names = approvedThemeNames();

        if (std::find(names.begin(), names.end(), command.argument) == names.end())
        {
            return failed("no approved theme '" + command.argument + "'");
        }

        if (!target_.applyTheme(command.argument))
        {
            return failed("could not apply theme '" + command.argument + "'");
        }

        return succeeded();
    }

    case CommandKind::listStates:
    {
        std::vector<std::string> items;
        items.reserve(approvedWindowStates().size());

        for (const ApprovedWindowState& state : approvedWindowStates())
        {
            items.push_back(state.id + " " + state.description);
        }

        return succeeded(std::move(items));
    }

    case CommandKind::listObjects:
    {
        std::vector<std::string> items;
        items.reserve(approvedClickTargets().size());

        for (const ApprovedClickTarget& target : approvedClickTargets())
        {
            items.push_back(target.id + " " + target.description);
        }

        return succeeded(std::move(items));
    }

    case CommandKind::status:
    {
        const std::string current = target_.currentStateId();

        // An empty id is not an error: the application is simply not in an
        // approved state, which is true at startup and after a click has
        // changed things. Saying so plainly beats inventing a state name.
        // A second item, so an existing reader that takes the first is
        // unaffected: `input` or `no-input`, which is what a harness waits on
        // before photographing a tool that has to be analysing something.
        return succeeded(
            {current.empty() ? std::string{"none"} : current,
             target_.hasInput() ? std::string{"input"} : std::string{"no-input"}});
    }

    case CommandKind::quit:
        finished_ = true;
        target_.requestQuit();

        return succeeded();
    }

    return failed("unhandled command");
}

Response TestControlSession::handleLine(const std::string& line)
{
    return handle(parseCommand(line));
}
} // namespace testcontrol
