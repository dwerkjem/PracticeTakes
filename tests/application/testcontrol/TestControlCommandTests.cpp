#include <catch2/catch_test_macros.hpp>

#include "application/testcontrol/TestControlCommand.h"

using namespace testcontrol;

TEST_CASE("a blank line is not an error", "[testcontrol][command]")
{
    // A line-oriented reader will see these; they should be skipped, not
    // reported as malformed input.
    for (const char* line : {"", "   ", "\t", "\r\n"})
    {
        INFO("line '" << line << "'");
        CHECK(parseCommand(line).kind == CommandKind::blank);
    }
}

TEST_CASE("open-state parses its state id", "[testcontrol][command]")
{
    const Command command = parseCommand("open-state tuner-docked");

    CHECK(command.kind == CommandKind::openState);
    CHECK(command.argument == "tuner-docked");
    CHECK(command.error.empty());
}

TEST_CASE("click parses its object id", "[testcontrol][command]")
{
    const Command command = parseCommand("click tools-button");

    CHECK(command.kind == CommandKind::click);
    CHECK(command.argument == "tools-button");
}

TEST_CASE("surrounding and repeated whitespace is tolerated", "[testcontrol][command]")
{
    // The line arrives over a pipe, so ragged spacing is expected. This is the
    // one place the parser is forgiving.
    const Command command = parseCommand("   open-state    tuner-docked   ");

    CHECK(command.kind == CommandKind::openState);
    CHECK(command.argument == "tuner-docked");
}

TEST_CASE("the argument-less verbs parse", "[testcontrol][command]")
{
    CHECK(parseCommand("list-states").kind == CommandKind::listStates);
    CHECK(parseCommand("list-objects").kind == CommandKind::listObjects);
    CHECK(parseCommand("status").kind == CommandKind::status);
    CHECK(parseCommand("quit").kind == CommandKind::quit);
}

TEST_CASE("an unknown verb is rejected rather than guessed at", "[testcontrol][command]")
{
    // Closed vocabulary: a harness that has drifted from the application must
    // fail loudly instead of appearing to work.
    const Command command = parseCommand("open-window tuner");

    CHECK(command.kind == CommandKind::invalid);
    CHECK(command.error.find("open-window") != std::string::npos);
}

TEST_CASE("a verb needing an argument rejects a missing one", "[testcontrol][command]")
{
    for (const char* line : {"open-state", "click"})
    {
        INFO("line '" << line << "'");

        const Command command = parseCommand(line);

        CHECK(command.kind == CommandKind::invalid);
        CHECK_FALSE(command.error.empty());
    }
}

TEST_CASE("a trailing extra argument is rejected", "[testcontrol][command]")
{
    // This is a machine interface, so silently ignoring an extra token would
    // hide a harness bug rather than surface it.
    const Command command = parseCommand("open-state tuner-docked spectrogram-docked");

    CHECK(command.kind == CommandKind::invalid);
    CHECK_FALSE(command.error.empty());
}

TEST_CASE("an argument-less verb rejects an argument", "[testcontrol][command]")
{
    const Command command = parseCommand("status now");

    CHECK(command.kind == CommandKind::invalid);
    CHECK_FALSE(command.error.empty());
}

TEST_CASE("verbs round-trip through their wire names", "[testcontrol][command]")
{
    for (const CommandKind kind : {CommandKind::openState, CommandKind::click})
    {
        const Command command = parseCommand(commandVerb(kind) + " some-id");

        INFO("verb " << commandVerb(kind));
        CHECK(command.kind == kind);
    }

    for (const CommandKind kind :
         {CommandKind::listStates, CommandKind::listObjects, CommandKind::status,
          CommandKind::quit})
    {
        INFO("verb " << commandVerb(kind));
        CHECK(parseCommand(commandVerb(kind)).kind == kind);
    }
}

TEST_CASE("kinds with no wire form report none", "[testcontrol][command]")
{
    CHECK(commandVerb(CommandKind::blank).empty());
    CHECK(commandVerb(CommandKind::invalid).empty());
}

TEST_CASE("an invalid command carries no argument", "[testcontrol][command]")
{
    const Command command = parseCommand("nonsense");

    CHECK(command.kind == CommandKind::invalid);
    CHECK(command.argument.empty());
}
