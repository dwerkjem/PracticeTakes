#include <catch2/catch_test_macros.hpp>

#include "platform/midi/MidiRecoveryPolicy.h"

#include <optional>
#include <string>
#include <vector>

// The whole truth table, as AudioRecoveryPolicyTests does for audio. The point
// of splitting the decision out of the service is that it can be covered
// exhaustively; covering it partly would waste the split.

TEST_CASE("nothing is opened when no device is selected", "[midi][recovery]")
{
    for (const bool isOpen : {false, true})
    {
        for (const bool isPresent : {false, true})
        {
            CHECK_FALSE(MidiRecoveryPolicy::shouldAttemptOpen(false, isOpen, isPresent));
        }
    }
}

TEST_CASE("nothing is opened when the selected device is already open", "[midi][recovery]")
{
    CHECK_FALSE(MidiRecoveryPolicy::shouldAttemptOpen(true, true, true));
    CHECK_FALSE(MidiRecoveryPolicy::shouldAttemptOpen(true, true, false));
}

TEST_CASE("an absent device is not retried", "[midi][recovery]")
{
    // Retrying an open against a keyboard that is not plugged in would call into
    // the backend on every scan for as long as the application runs, to no
    // effect. The enumeration is what changes when the user plugs it back in.
    CHECK_FALSE(MidiRecoveryPolicy::shouldAttemptOpen(true, false, false));
}

TEST_CASE("a selected, present, unopened device is opened", "[midi][recovery]")
{
    CHECK(MidiRecoveryPolicy::shouldAttemptOpen(true, false, true));
}

TEST_CASE("nothing is closed when nothing is open", "[midi][recovery]")
{
    for (const bool isSelected : {false, true})
    {
        for (const bool isPresent : {false, true})
        {
            CHECK_FALSE(MidiRecoveryPolicy::shouldClose(false, isSelected, isPresent));
        }
    }
}

TEST_CASE("an open device that is still selected and present stays open", "[midi][recovery]")
{
    CHECK_FALSE(MidiRecoveryPolicy::shouldClose(true, true, true));
}

TEST_CASE("an open device the user has moved away from is closed", "[midi][recovery]")
{
    CHECK(MidiRecoveryPolicy::shouldClose(true, false, true));
    CHECK(MidiRecoveryPolicy::shouldClose(true, false, false));
}

TEST_CASE("an open device that has gone away is closed", "[midi][recovery]")
{
    // The unplug case: still the one the user chose, no longer there.
    CHECK(MidiRecoveryPolicy::shouldClose(true, true, false));
}

// Which enumerated device is the one the user chose.
//
// ALSA names a port by sequencer client and port number, and a client number is
// handed out afresh whenever a device appears -- so the identifier a keyboard
// was chosen under can come back belonging to something else, and the keyboard
// can come back under another. Both were seen against a real ALSA sequencer
// before any keyboard was: the application opened "Some Other Device" under
// the keyboard's remembered name, and ignored the keyboard. The name is the
// half of the selection that survives, so it is checked as well.
namespace
{
using Listing = MidiRecoveryPolicy::DeviceListing;
using MidiRecoveryPolicy::resolveSelectedDevice;
} // namespace

TEST_CASE("nothing resolves when no device is selected", "[midi][recovery]")
{
    const std::vector<Listing> present{{"129-0", "Piano"}};

    CHECK_FALSE(resolveSelectedDevice("", "", present).has_value());
    CHECK_FALSE(resolveSelectedDevice("", "Piano", present).has_value());
}

TEST_CASE(
    "the chosen identifier resolves while it still carries the chosen name",
    "[midi][recovery]")
{
    const std::vector<Listing> present{{"128-0", "Other"}, {"129-0", "Piano"}};

    CHECK(resolveSelectedDevice("129-0", "Piano", present) == std::optional<std::string>("129-0"));
}

TEST_CASE("an identifier now held by another device does not resolve to it", "[midi][recovery]")
{
    // The keyboard's old address, taken by something else: opening it would be
    // choosing a different device on the user's behalf.
    const std::vector<Listing> present{{"129-0", "Some Other Device"}};

    CHECK_FALSE(resolveSelectedDevice("129-0", "Piano", present).has_value());
}

TEST_CASE("the chosen device is found by name when its address has moved", "[midi][recovery]")
{
    const std::vector<Listing> present{{"129-0", "Some Other Device"}, {"131-0", "Piano"}};

    CHECK(resolveSelectedDevice("129-0", "Piano", present) == std::optional<std::string>("131-0"));
    CHECK(resolveSelectedDevice("140-0", "Piano", present) == std::optional<std::string>("131-0"));
}

TEST_CASE("two devices sharing the chosen name are not guessed between", "[midi][recovery]")
{
    const std::vector<Listing> present{{"130-0", "Piano"}, {"131-0", "Piano"}};

    // Neither is known to be the one chosen, so neither is opened...
    CHECK_FALSE(resolveSelectedDevice("129-0", "Piano", present).has_value());

    // ...unless the chosen address is one of them, which settles it.
    CHECK(resolveSelectedDevice("131-0", "Piano", present) == std::optional<std::string>("131-0"));
}

TEST_CASE("a selection with no remembered name is matched by identifier alone", "[midi][recovery]")
{
    const std::vector<Listing> present{{"129-0", "Piano"}};

    CHECK(resolveSelectedDevice("129-0", "", present) == std::optional<std::string>("129-0"));
    CHECK_FALSE(resolveSelectedDevice("130-0", "", present).has_value());
}

TEST_CASE("nothing resolves when the chosen device is absent", "[midi][recovery]")
{
    CHECK_FALSE(resolveSelectedDevice("129-0", "Piano", {}).has_value());
    CHECK_FALSE(resolveSelectedDevice("129-0", "Piano", {{"128-0", "Other"}}).has_value());
}
