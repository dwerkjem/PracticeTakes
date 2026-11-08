#pragma once

#include "RealtimeSafety.h"

// Something the audio callback runs once per block, to produce sound or keep
// time: the practice clock (openspec/changes/archive/2026-11-08-practice-clock).
//
// No JUCE dependency, so a renderer is built and tested without a device.
// `AudioInputService` hosts exactly one, published to the callback atomically
// (`setRealtimeRenderer`). One, not a list: the clock is the only thing that
// renders, and whatever else comes to make a sound -- accompaniment, a drone --
// is rendered *by the clock*, so the timeline has one owner. A list would invite
// a second clock (design.md, "One real-time renderer").
//
// `render` runs on the real-time audio thread, under the contract in
// docs/development/performance/audio-thread-safety.md: no allocation, no lock,
// no blocking call, no I/O, bounded work.
struct RenderBlock
{
    // The input the analysis tools are given this block: the synthetic tone
    // when one is asked for, otherwise the device's first input channel, before
    // `inputGain` is applied. Null while the input is muted or when there is
    // none. Valid for `frames` samples during the call only.
    const float* input = nullptr;
    float inputGain = 1.0f;

    // The output channels, already cleared. A renderer adds to them. Null, with
    // `outputChannels` zero, when the device has no output open or the block
    // comes from the synthetic-input timer: the clock still advances, silently.
    float* const* outputs = nullptr;
    int outputChannels = 0;

    int frames = 0;
    double sampleRate = 44100.0;

    // `juce::Time::getMillisecondCounterHiRes()` when the callback began: the
    // same clock MidiInputService stamps events with, so an event from the
    // keyboard can be placed on this block's frames (#40).
    double timeMilliseconds = 0.0;
};

class RealtimeRenderer
{
  public:
    virtual ~RealtimeRenderer() = default;

    virtual void render(RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING = 0;

  protected:
    RealtimeRenderer() = default;
    RealtimeRenderer(const RealtimeRenderer&) = default;
    RealtimeRenderer& operator=(const RealtimeRenderer&) = default;
};
