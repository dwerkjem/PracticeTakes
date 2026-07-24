#include <catch2/catch_test_macros.hpp>

#include "audio/AudioRecoveryPolicy.h"

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
