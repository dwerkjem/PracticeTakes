#pragma once

// Whether the MIDI input device should be (re)opened, as a pure decision.
//
// The same shape as AudioRecoveryPolicy next door, and for the same reason: the
// deciding is four booleans and a comparison, which a test can cover
// exhaustively, while the doing needs a device backend that a test cannot
// arrange. Keeping them apart is what lets the truth table be checked.
namespace MidiRecoveryPolicy
{
// `hasSelectedDevice` -- the user has chosen one, or one was restored from
//     settings. With none, there is nothing to open and nothing is wrong.
// `isSelectedDeviceOpen` -- it is open and delivering.
// `isSelectedDevicePresent` -- it appears in the current enumeration.
[[nodiscard]] constexpr bool shouldAttemptOpen(
    bool hasSelectedDevice,
    bool isSelectedDeviceOpen,
    bool isSelectedDevicePresent) noexcept
{
    if (!hasSelectedDevice || isSelectedDeviceOpen)
    {
        return false;
    }

    // Only when the device is actually there. Retrying an open against a
    // keyboard that is not plugged in would be a call into the backend every
    // scan for as long as the application runs, to no effect: the enumeration
    // is what changes when the user plugs it back in, and that is cheap to
    // check.
    return isSelectedDevicePresent;
}

// Whether what is open should be closed: the device the user chose is no longer
// the one that is open, or the open one has gone away.
[[nodiscard]] constexpr bool
shouldClose(bool isSomethingOpen, bool openDeviceIsSelected, bool openDeviceIsPresent) noexcept
{
    if (!isSomethingOpen)
    {
        return false;
    }

    return !openDeviceIsSelected || !openDeviceIsPresent;
}
} // namespace MidiRecoveryPolicy
