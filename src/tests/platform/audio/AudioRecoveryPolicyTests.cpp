#include <catch2/catch_test_macros.hpp>

#include "platform/audio/AudioRecoveryPolicy.h"

TEST_CASE("audio recovery never replaces an open backend", "[audio][recovery]")
{
    CHECK_FALSE(AudioRecoveryPolicy::shouldAttemptRecovery(false, true, true, true));
    CHECK_FALSE(AudioRecoveryPolicy::shouldAttemptRecovery(true, true, true, true));
}

TEST_CASE("audio recovery waits for a device before retrying", "[audio][recovery]")
{
    CHECK_FALSE(AudioRecoveryPolicy::shouldAttemptRecovery(false, false, false, false));
    CHECK_FALSE(AudioRecoveryPolicy::shouldAttemptRecovery(false, true, false, false));
}

TEST_CASE("audio recovery retries only after the backend stops", "[audio][recovery]")
{
    CHECK(AudioRecoveryPolicy::shouldAttemptRecovery(false, true, false, true));
    CHECK(AudioRecoveryPolicy::shouldAttemptRecovery(false, false, false, true));
}

TEST_CASE("audio recovery policy matches its complete input state table", "[audio][recovery]")
{
    for (const auto hasUsableInput : {false, true})
    {
        for (const auto hasCurrentDevice : {false, true})
        {
            for (const auto isCurrentDeviceOpen : {false, true})
            {
                for (const auto hasEnumeratedInput : {false, true})
                {
                    CAPTURE(
                        hasUsableInput, hasCurrentDevice, isCurrentDeviceOpen, hasEnumeratedInput);
                    const auto expected = !hasUsableInput && hasEnumeratedInput &&
                                          (!hasCurrentDevice || !isCurrentDeviceOpen);
                    CHECK(
                        AudioRecoveryPolicy::shouldAttemptRecovery(
                            hasUsableInput, hasCurrentDevice, isCurrentDeviceOpen,
                            hasEnumeratedInput) == expected);
                }
            }
        }
    }
}
