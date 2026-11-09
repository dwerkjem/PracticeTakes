#pragma once

#include <juce_core/juce_core.h>

#include <memory>

// The audio device state in a settings transfer file. Every setting travels,
// the output device too: a device that is not on the importing machine falls
// back to the default, as a missing input always has.
//
// A bundle from before outputs travelled names none, because its export left
// the output out (openspec/changes/archive/2026-11-08-practice-clock, design.md,
// Migration Plan). Importing one keeps this machine's output rather than
// dropping it, which is what this is for.
//
// The state is JUCE's own `DEVICESETUP` XML, which names the output in
// `audioOutputDeviceName` and its channels in `audioDeviceOutChans`, or names
// one device for both in `audioDeviceName`.
namespace AudioDeviceStateTransfer
{
// An imported state, with this machine's output device put back. Null when the
// imported text is not a device state.
[[nodiscard]] inline std::unique_ptr<juce::XmlElement>
withLocalOutputDevice(const juce::String& imported, const juce::XmlElement* local)
{
    auto xml = juce::parseXML(imported);
    if (xml == nullptr || !xml->hasTagName("DEVICESETUP"))
    {
        return nullptr;
    }

    if (const auto shared = xml->getStringAttribute("audioDeviceName"); shared.isNotEmpty())
    {
        xml->setAttribute("audioInputDeviceName", shared);
        xml->removeAttribute("audioDeviceName");
    }
    xml->removeAttribute("audioOutputDeviceName");
    xml->removeAttribute("audioDeviceOutChans");

    if (local != nullptr)
    {
        const auto output = local->getStringAttribute("audioDeviceName").isNotEmpty()
                                ? local->getStringAttribute("audioDeviceName")
                                : local->getStringAttribute("audioOutputDeviceName");
        if (output.isNotEmpty())
        {
            xml->setAttribute("audioOutputDeviceName", output);
        }
        if (local->hasAttribute("audioDeviceOutChans"))
        {
            xml->setAttribute(
                "audioDeviceOutChans", local->getStringAttribute("audioDeviceOutChans"));
        }
    }
    return xml;
}
} // namespace AudioDeviceStateTransfer
