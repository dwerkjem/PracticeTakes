#include <catch2/catch_test_macros.hpp>

#include "application/configuration/AudioDeviceStateTransfer.h"

using namespace AudioDeviceStateTransfer;

TEST_CASE("an import keeps this machine's output device", "[settings][transfer][audio]")
{
    const juce::String imported =
        R"(<DEVICESETUP deviceType="ALSA" audioInputDeviceName="Their mic" )"
        R"(audioOutputDeviceName="Their speakers"/>)";
    juce::XmlElement local{"DEVICESETUP"};
    local.setAttribute("audioInputDeviceName", "My mic");
    local.setAttribute("audioOutputDeviceName", "My headphones");
    local.setAttribute("audioDeviceOutChans", "11");

    const auto merged = withLocalOutputDevice(imported, &local);
    REQUIRE(merged != nullptr);
    CHECK(merged->getStringAttribute("audioInputDeviceName") == "Their mic");
    CHECK(merged->getStringAttribute("audioOutputDeviceName") == "My headphones");
    CHECK(merged->getStringAttribute("audioDeviceOutChans") == "11");
}

TEST_CASE("an import with no local state names no output", "[settings][transfer][audio]")
{
    const juce::String imported =
        R"(<DEVICESETUP audioInputDeviceName="Mic" audioOutputDeviceName="Theirs"/>)";

    const auto merged = withLocalOutputDevice(imported, nullptr);
    REQUIRE(merged != nullptr);
    CHECK_FALSE(merged->hasAttribute("audioOutputDeviceName"));
    CHECK(withLocalOutputDevice("not xml", nullptr) == nullptr);
}
