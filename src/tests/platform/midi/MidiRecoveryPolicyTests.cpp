#include <catch2/catch_test_macros.hpp>

#include "platform/midi/MidiRecoveryPolicy.h"

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
