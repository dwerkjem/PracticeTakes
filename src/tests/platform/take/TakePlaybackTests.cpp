#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "platform/take/TakePlayback.h"

// What a take sounds when played back: its keyboard as notes, with the pedal
// as it sounded, and its microphone at the device's rate.
namespace
{
take::KeyboardEvent
key(transport::Frame frame, midi::MidiEventKind kind, int note, int velocity = 90)
{
    take::KeyboardEvent event;
    event.frame = frame;
    event.event.kind = kind;
    event.event.noteNumber = note;
    event.event.velocity = velocity;
    return event;
}

take::KeyboardEvent pedal(transport::Frame frame, int position)
{
    take::KeyboardEvent event;
    event.frame = frame;
    event.event.kind = midi::MidiEventKind::sustainPedal;
    event.event.pedalPosition = position;
    return event;
}

std::shared_ptr<take::Take> keyboardTake()
{
    auto recorded = std::make_shared<take::Take>();
    recorded->sampleRate = 48000.0;
    recorded->lastFrame = 48000;
    recorded->usedKeyboard = true;
    recorded->keyboard = {
        key(1000, midi::MidiEventKind::noteOn, 60, 100),
        key(5000, midi::MidiEventKind::noteOff, 60),
        // Released under the pedal: sounds until the pedal lifts.
        pedal(6000, 120),
        key(7000, midi::MidiEventKind::noteOn, 64, 50),
        key(8000, midi::MidiEventKind::noteOff, 64),
        pedal(20000, 0),
        // Still held at the end.
        key(30000, midi::MidiEventKind::noteOn, 67, 70),
    };
    return recorded;
}

const transport::PlaybackEvent*
find(const std::vector<transport::PlaybackEvent>& events, int keyNumber, bool on)
{
    for (const auto& event : events)
    {
        if (event.key == keyNumber && event.on == on)
        {
            return &event;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("a take's keyboard plays as it sounded, the pedal included", "[take][playback]")
{
    const auto events = take::keyboardPlaybackOf(*keyboardTake(), 48000.0, 64, 5);
    REQUIRE(events.size() == 6);
    for (const auto& event : events)
    {
        CHECK(event.track == 5);
    }

    const auto* firstOn = find(events, 60, true);
    const auto* firstOff = find(events, 60, false);
    REQUIRE(firstOn != nullptr);
    REQUIRE(firstOff != nullptr);
    CHECK(firstOn->frame == 1000);
    CHECK(firstOn->velocity == 100);
    // Released before the pedal went down: ends with the finger.
    CHECK(firstOff->frame == 5000);

    // Released under the pedal: sounds until it lifts.
    CHECK(find(events, 64, true)->velocity == 50);
    CHECK(find(events, 64, false)->frame == 20000);

    // Still held when the take ended: stops at the end.
    CHECK(find(events, 67, false)->frame == 48000);

    // In time order.
    for (std::size_t index = 1; index < events.size(); ++index)
    {
        CHECK(events[index - 1].frame <= events[index].frame);
    }
}

TEST_CASE("a pedal the reader counts as up does not hold a note", "[take][playback]")
{
    // A pedal pressed to 120, against a threshold of 127, is up.
    const auto events = take::keyboardPlaybackOf(*keyboardTake(), 48000.0, 127, 0);
    CHECK(find(events, 64, false)->frame == 8000);
}

TEST_CASE("a take at another rate is played at the device's", "[take][playback]")
{
    auto recorded = keyboardTake();
    recorded->usedMicrophone = true;
    recorded->audioStart = -4800;
    recorded->audio.resize(9600);
    for (std::size_t index = 0; index < recorded->audio.size(); ++index)
    {
        recorded->audio[index] = static_cast<float>(index) / 9600.0f;
    }

    // The same rate shares the take's own samples.
    const auto same = take::takePlaybackOf(recorded, 48000.0, 64, 0);
    CHECK(same.audio == recorded->audio.data());
    CHECK(same.audioFrames == 9600);
    CHECK(same.audioStart == -4800);
    CHECK(same.endFrame == 48000);

    // At half the rate: half the frames, everything placed at half the frame.
    const auto half = take::takePlaybackOf(recorded, 24000.0, 64, 0);
    CHECK(half.audio != recorded->audio.data());
    CHECK(half.audioFrames == 4800);
    CHECK(half.audioStart == -2400);
    CHECK(half.endFrame == 24000);
    CHECK(find(half.keyboard, 60, true)->frame == 500);
    // The ramp survives, sample for sample.
    CHECK(std::abs(half.audio[1000] - (2000.0f / 9600.0f)) < 1e-4f);
    // The take can go; what plays holds its own samples.
    recorded.reset();
    CHECK(std::abs(half.audio[4799] - (9598.0f / 9600.0f)) < 1e-4f);
}

TEST_CASE("nothing to play is nothing", "[take][playback]")
{
    const auto material = take::takePlaybackOf(std::make_shared<take::Take>(), 44100.0, 64, 0);
    CHECK(material.audio == nullptr);
    CHECK(material.keyboard.empty());
    CHECK(take::takePlaybackOf(nullptr, 44100.0, 64, 0).audio == nullptr);
    CHECK(take::resampled({}, 48000.0, 44100.0).empty());
}
