#pragma once

#include "platform/audio/AudioInputService.h"

// Granted access by a friend declaration in AudioInputService.h. The callback
// is private because only a device may call it; a verification harness is the
// one exception, and it is named rather than implied.
//
// Shared rather than redefined per test file: the friend declaration grants
// access to exactly this name at global scope, so every translation unit that
// needs it must see the same definition -- one header, included wherever the
// real callback needs driving, keeps that true instead of relying on multiple
// copies staying in sync by hand.
struct AudioInputServiceCallbackAccess
{
    static void invoke(
        AudioInputService& service,
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples)
    {
        const juce::AudioIODeviceCallbackContext context{};
        service.audioDeviceIOCallbackWithContext(
            inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples,
            context);
    }

    // What every JUCE backend calls synchronously, on the caller's thread,
    // before the real-time thread can invoke the callback above for a device
    // session -- confirmed against ALSAAudioIODevice::start() rather than
    // assumed. `nullptr` is a real caller shape: the service already treats a
    // null device as "use the defaults".
    static void aboutToStart(AudioInputService& service, juce::AudioIODevice* device = nullptr)
    {
        service.audioDeviceAboutToStart(device);
    }

    static void stopped(AudioInputService& service)
    {
        service.audioDeviceStopped();
    }

    static bool toneSourceRunning(const AudioInputService& service)
    {
        return service.toneSourceRunning.load(std::memory_order_acquire);
    }
};
