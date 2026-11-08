#pragma once

#include <juce_core/juce_core.h>

#include <memory>

// What of the audio device state travels in a settings transfer file, and what
// stays with the machine (openspec/changes/archive/2026-11-08-practice-clock, design.md,
// Migration Plan).
//
// The output device -- the speakers or headphones the metronome plays through
// -- belongs to this machine, exactly as the MIDI keyboard does: importing
// another machine's would name a device that is not here, or silently replace
// a working one. So an export leaves it out, and an import keeps this
// machine's. The input device keeps travelling, as it always has.
//
// The state is JUCE's own `DEVICESETUP` XML, which names the output in
// `audioOutputDeviceName` and its channels in `audioDeviceOutChans`, or names
// one device for both in `audioDeviceName`.
namespace AudioDeviceStateTransfer
{
// The state with no output device in it, for an export. Text that is not a
// device state is returned unchanged: the codec carries it either way, and
// deciding what is valid is the importer's job.
[[nodiscard]] inline juce::String withoutOutputDevice(const juce::String& state)
{
    const auto xml = juce::parseXML(state);
    if (xml == nullptr || !xml->hasTagName("DEVICESETUP"))
    {
        return state;
    }

    // Left exactly as written when there is no output in it to remove, so a
    // state that never named one round-trips byte for byte.
    if (!xml->hasAttribute("audioDeviceName") && !xml->hasAttribute("audioOutputDeviceName") &&
        !xml->hasAttribute("audioDeviceOutChans"))
    {
        return state;
    }

    if (const auto shared = xml->getStringAttribute("audioDeviceName"); shared.isNotEmpty())
    {
        xml->setAttribute("audioInputDeviceName", shared);
        xml->removeAttribute("audioDeviceName");
    }
    xml->removeAttribute("audioOutputDeviceName");
    xml->removeAttribute("audioDeviceOutChans");
    return xml->toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

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
