#include "TestControlCommand.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace testcontrol
{
namespace
{
[[nodiscard]] bool isSpace(unsigned char character) noexcept
{
    return std::isspace(character) != 0;
}

// Split on runs of whitespace, discarding empties. A pipe can deliver ragged
// spacing, so this is the one place the parser is forgiving.
[[nodiscard]] std::vector<std::string> tokenise(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;

    for (const char character : line)
    {
        if (isSpace(static_cast<unsigned char>(character)))
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }

            continue;
        }

        current.push_back(character);
    }

    if (!current.empty())
    {
        tokens.push_back(current);
    }

    return tokens;
}

[[nodiscard]] Command invalidCommand(std::string reason)
{
    Command command;
    command.kind = CommandKind::invalid;
    command.error = std::move(reason);

    return command;
}

// A verb that takes exactly one argument.
[[nodiscard]] Command
withArgument(CommandKind kind, const std::vector<std::string>& tokens, const char* what)
{
    if (tokens.size() < 2)
    {
        return invalidCommand(std::string{"'"} + tokens[0] + "' needs a " + what);
    }

    if (tokens.size() > 2)
    {
        return invalidCommand(std::string{"'"} + tokens[0] + "' takes exactly one " + what);
    }

    Command command;
    command.kind = kind;
    command.argument = tokens[1];

    return command;
}

// A verb that takes no arguments.
[[nodiscard]] Command withoutArguments(CommandKind kind, const std::vector<std::string>& tokens)
{
    if (tokens.size() > 1)
    {
        return invalidCommand(std::string{"'"} + tokens[0] + "' takes no arguments");
    }

    Command command;
    command.kind = kind;

    return command;
}
} // namespace

Command parseCommand(const std::string& line)
{
    const std::vector<std::string> tokens = tokenise(line);

    if (tokens.empty())
    {
        Command command;
        command.kind = CommandKind::blank;

        return command;
    }

    const std::string& verb = tokens.front();

    if (verb == "open-state")
    {
        return withArgument(CommandKind::openState, tokens, "state id");
    }

    if (verb == "click")
    {
        return withArgument(CommandKind::click, tokens, "object id");
    }

    if (verb == "list-states")
    {
        return withoutArguments(CommandKind::listStates, tokens);
    }

    if (verb == "list-objects")
    {
        return withoutArguments(CommandKind::listObjects, tokens);
    }

    if (verb == "status")
    {
        return withoutArguments(CommandKind::status, tokens);
    }

    if (verb == "quit")
    {
        return withoutArguments(CommandKind::quit, tokens);
    }

    // Closed vocabulary: an unrecognised verb is an error rather than a
    // best-effort guess, so a harness that has drifted fails loudly.
    return invalidCommand("unknown command '" + verb + "'");
}

std::string commandVerb(CommandKind kind)
{
    switch (kind)
    {
    case CommandKind::openState:
        return "open-state";
    case CommandKind::click:
        return "click";
    case CommandKind::listStates:
        return "list-states";
    case CommandKind::listObjects:
        return "list-objects";
    case CommandKind::status:
        return "status";
    case CommandKind::quit:
        return "quit";
    case CommandKind::blank:
    case CommandKind::invalid:
        return {};
    }

    return {};
}
} // namespace testcontrol
