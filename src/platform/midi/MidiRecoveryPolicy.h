#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

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

// A device as enumerated: the identifier the backend opens it by, and the name
// it shows. Plain strings, so the choice below is checkable without a backend.
struct DeviceListing
{
    std::string identifier;
    std::string name;
};

// Which of the `present` devices is the one the user chose, if any.
//
// An identifier alone is not an identity. ALSA names a port by sequencer client
// and port number, and a client number is handed out afresh each time a device
// appears: the keyboard can return under another identifier, and its old one
// can be given to something else. So the chosen identifier counts only while it
// still carries the chosen name, and otherwise the chosen name is looked for --
// but only when exactly one device carries it. Two devices with one name could
// each be the keyboard, and opening the wrong one is choosing a device on the
// user's behalf, which the selection requirement forbids. A selection with no
// remembered name is matched by identifier alone, as it always was.
[[nodiscard]] inline std::optional<std::string> resolveSelectedDevice(
    const std::string& chosenIdentifier,
    const std::string& chosenName,
    const std::vector<DeviceListing>& present)
{
    if (chosenIdentifier.empty())
    {
        return std::nullopt;
    }

    const auto atChosenIdentifier = std::find_if(
        present.begin(), present.end(), [&chosenIdentifier](const DeviceListing& device)
        { return device.identifier == chosenIdentifier; });

    if (atChosenIdentifier != present.end() &&
        (chosenName.empty() || atChosenIdentifier->name == chosenName))
    {
        return chosenIdentifier;
    }

    if (chosenName.empty())
    {
        return std::nullopt;
    }

    const auto carriesName = [&chosenName](const DeviceListing& device)
    { return device.name == chosenName; };

    if (std::count_if(present.begin(), present.end(), carriesName) != 1)
    {
        return std::nullopt;
    }

    return std::find_if(present.begin(), present.end(), carriesName)->identifier;
}
} // namespace MidiRecoveryPolicy
