#pragma once

namespace FeedbackInvitationPolicy
{
constexpr int successfulUsesBeforeInvitation = 3;

struct State
{
    int successfulUses = 0;
    bool invitationShown = false;
    bool invitationsDisabled = false;
};

[[nodiscard]] constexpr bool shouldInvite(const State& state, bool isLiveSessionActive)
{
    return !isLiveSessionActive && !state.invitationShown && !state.invitationsDisabled &&
           state.successfulUses >= successfulUsesBeforeInvitation;
}
} // namespace FeedbackInvitationPolicy
