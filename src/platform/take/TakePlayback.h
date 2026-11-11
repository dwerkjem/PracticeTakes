#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../midi/NoteSoundingTracker.h"
#include "../transport/PlaybackEvents.h"
#include "Take.h"

// What playing a take back sounds, on the clock's timeline at the device's rate
// (no JUCE dependency; openspec/changes/archive/2026-11-11-take-playback, decisions 3 and 4).
//
// The microphone, as samples and the frame they start at; and the keyboard, as
// the note-ons and note-offs the synthesiser plays, with the sustain pedal
// applied as the review applies it, so a note held by the pedal sounds until
// the pedal lifted.
namespace take
{
struct TakePlaybackMaterial
{
    // The microphone at the device's rate, and what keeps it alive: the take
    // itself when the rates match, a resampled copy otherwise. Null samples
    // for a take with no microphone.
    std::shared_ptr<const void> audioOwner;
    const float* audio = nullptr;
    std::size_t audioFrames = 0;
    transport::Frame audioStart = 0;

    // The keyboard, on `track`, in time order, offs before ons at a frame.
    std::vector<transport::PlaybackEvent> keyboard;

    // Where the take was started from and where it ended, at the device's
    // rate.
    transport::Frame endFrame = 0;
};

// A frame of a take recorded at `takeRate`, at `deviceRate`.
[[nodiscard]] inline transport::Frame
framesAtRate(transport::Frame frame, double takeRate, double deviceRate) noexcept
{
    if (takeRate <= 0.0 || deviceRate <= 0.0 || takeRate == deviceRate)
    {
        return frame;
    }
    return static_cast<transport::Frame>(
        std::llround(static_cast<double>(frame) * deviceRate / takeRate));
}

// `samples` from `fromRate` to `toRate`, by linear interpolation: enough for
// listening back, and done once, here, rather than on the audio thread.
[[nodiscard]] inline std::vector<float>
resampled(const std::vector<float>& samples, double fromRate, double toRate)
{
    if (samples.empty() || fromRate <= 0.0 || toRate <= 0.0)
    {
        return {};
    }
    const double step = fromRate / toRate;
    const auto count =
        static_cast<std::size_t>(std::floor(static_cast<double>(samples.size() - 1) / step) + 1.0);
    std::vector<float> result(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        const double at = static_cast<double>(index) * step;
        const auto before = static_cast<std::size_t>(at);
        const auto after = std::min(before + 1, samples.size() - 1);
        const auto fraction = static_cast<float>(at - static_cast<double>(before));
        result[index] = samples[before] + ((samples[after] - samples[before]) * fraction);
    }
    return result;
}

// The keyboard of `recorded` as notes to play: each key from its press to the
// end of its sound, the pedal held as `pedalThreshold` says. Frames at
// `deviceRate`; every event on `track`.
[[nodiscard]] inline std::vector<transport::PlaybackEvent>
keyboardPlaybackOf(const Take& recorded, double deviceRate, int pedalThreshold, std::uint8_t track)
{
    midi::NoteSoundingTracker tracker;
    tracker.setPedalThreshold(pedalThreshold);
    std::vector<midi::SoundedNote> notes;
    for (const KeyboardEvent& keyboard : recorded.keyboard)
    {
        midi::MidiEvent event = keyboard.event;
        // The tracker reads time from the device's stamp; a clock frame is as
        // good a unit, and the one the notes are wanted in.
        event.deviceTimestampSeconds = static_cast<double>(keyboard.frame);
        tracker.consume(event, notes);
    }
    // Anything still sounding stops where the take did.
    const transport::Frame last = std::max<transport::Frame>(
        recorded.lastFrame, recorded.keyboard.empty() ? 0 : recorded.keyboard.back().frame);
    tracker.invalidate(static_cast<double>(last), notes);

    std::vector<transport::PlaybackEvent> events;
    events.reserve(notes.size() * 2);
    const auto at = [&recorded, deviceRate](double frame)
    {
        return framesAtRate(
            static_cast<transport::Frame>(std::llround(frame)), recorded.sampleRate, deviceRate);
    };
    for (const midi::SoundedNote& note : notes)
    {
        const auto key = static_cast<std::uint8_t>(std::clamp(note.noteNumber, 0, 127));
        const auto velocity = static_cast<std::uint8_t>(std::clamp(note.velocity, 1, 127));
        const transport::Frame on = at(note.keyDownSeconds);
        const transport::Frame off = std::max(on + 1, at(note.soundingEndSeconds));
        events.push_back({on, track, key, true, velocity});
        events.push_back({off, track, key, false, 0});
    }
    std::stable_sort(
        events.begin(), events.end(),
        [](const transport::PlaybackEvent& one, const transport::PlaybackEvent& other)
        { return one.frame != other.frame ? one.frame < other.frame : (!one.on && other.on); });
    return events;
}

[[nodiscard]] inline TakePlaybackMaterial takePlaybackOf(
    const std::shared_ptr<const Take>& recorded,
    double deviceRate,
    int pedalThreshold,
    std::uint8_t keyboardTrack)
{
    TakePlaybackMaterial material;
    if (recorded == nullptr)
    {
        return material;
    }
    const double takeRate = recorded->sampleRate;
    const bool sameRate = takeRate <= 0.0 || deviceRate <= 0.0 || takeRate == deviceRate;
    if (!recorded->audio.empty())
    {
        if (sameRate)
        {
            material.audioOwner = recorded;
            material.audio = recorded->audio.data();
            material.audioFrames = recorded->audio.size();
        }
        else
        {
            auto copy = std::make_shared<const std::vector<float>>(
                resampled(recorded->audio, takeRate, deviceRate));
            material.audio = copy->data();
            material.audioFrames = copy->size();
            material.audioOwner = std::move(copy);
        }
        material.audioStart = framesAtRate(recorded->audioStart, takeRate, deviceRate);
    }
    material.keyboard = keyboardPlaybackOf(*recorded, deviceRate, pedalThreshold, keyboardTrack);
    material.endFrame = framesAtRate(recorded->lastFrame, takeRate, deviceRate);
    return material;
}
} // namespace take
