#pragma once

namespace AudioRecoveryPolicy
{
[[nodiscard]] constexpr bool shouldAttemptRecovery(
    bool hasUsableInput,
    bool hasCurrentDevice,
    bool isCurrentDeviceOpen,
    bool hasEnumeratedInput) noexcept
{
    if (hasUsableInput || !hasEnumeratedInput)
    {
        return false;
    }

    return !hasCurrentDevice || !isCurrentDeviceOpen;
}
} // namespace AudioRecoveryPolicy
