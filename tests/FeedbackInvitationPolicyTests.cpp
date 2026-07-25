#include <catch2/catch_test_macros.hpp>

#include "features/feedback/FeedbackInvitationPolicy.h"

TEST_CASE("feedback invitations wait for several successful uses")
{
    using namespace FeedbackInvitationPolicy;

    CHECK_FALSE(shouldInvite({successfulUsesBeforeInvitation - 1, false, false}, false));
    CHECK(shouldInvite({successfulUsesBeforeInvitation, false, false}, false));
}

TEST_CASE("feedback invitations never interrupt a live session")
{
    using namespace FeedbackInvitationPolicy;

    CHECK_FALSE(shouldInvite({successfulUsesBeforeInvitation, false, false}, true));
}

TEST_CASE("dismissed and disabled invitations remain suppressed")
{
    using namespace FeedbackInvitationPolicy;

    CHECK_FALSE(shouldInvite({successfulUsesBeforeInvitation, true, false}, false));
    CHECK_FALSE(shouldInvite({successfulUsesBeforeInvitation, false, true}, false));
}

TEST_CASE("feedback invitation remains eligible after the minimum use count")
{
    using namespace FeedbackInvitationPolicy;

    CHECK(shouldInvite({successfulUsesBeforeInvitation + 20, false, false}, false));
}

TEST_CASE("feedback invitation suppression conditions compose")
{
    using namespace FeedbackInvitationPolicy;

    CHECK_FALSE(shouldInvite({successfulUsesBeforeInvitation + 1, true, true}, true));
    CHECK_FALSE(shouldInvite({0, true, true}, false));
}
