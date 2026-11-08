#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../../platform/evaluation/ExpectedAttacks.h"
#include "../../platform/take/Take.h"
#include "../../platform/transport/ClockMath.h"

// A keyboard take of an open score with known mistakes, for the capture
// harness to review (openspec/changes/take-review/design.md, decision 5). No
// JUCE dependency.
//
// Nobody plays the harness's keyboard, so the picture of a review needs a take
// someone could have played: the part's notes through `bars` bars, at tempo,
// with a few of each kind of mistake on a fixed pattern. The pattern is over
// the part's attacks, so any score gets some of each.
namespace testcontrol
{
inline constexpr double syntheticOffsetSeconds = 0.06;

[[nodiscard]] inline take::Take syntheticKeyboardTake(
    std::shared_ptr<const score::Score> source,
    std::size_t part,
    std::int64_t bars,
    double sampleRate = 44100.0)
{
    take::Take result;
    if (source == nullptr || part >= source->parts.size() || bars <= 0)
    {
        return result;
    }

    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(*table, *source, transport::defaultScalePercent, sampleRate);
    result.table = table;
    result.sampleRate = sampleRate;
    result.scoreTitle = source->metadata.workTitle;
    result.usedKeyboard = true;

    const auto offset = static_cast<transport::Frame>(syntheticOffsetSeconds * sampleRate);
    const auto press = [&result](transport::Frame frame, int key)
    {
        take::KeyboardEvent event;
        event.frame = frame;
        event.event.kind = midi::MidiEventKind::noteOn;
        event.event.noteNumber = key;
        event.event.velocity = 80;
        result.keyboard.push_back(event);
    };

    std::vector<evaluation::ExpectedAttack> attacks = evaluation::expectedAttacksOf(*source, part);
    attacks.erase(
        std::find_if(
            attacks.begin(), attacks.end(), [bars](const evaluation::ExpectedAttack& attack)
            { return static_cast<std::int64_t>(attack.measureIndex) >= bars; }),
        attacks.end());
    std::vector<transport::Frame> frames;
    frames.reserve(attacks.size());
    for (const evaluation::ExpectedAttack& attack : attacks)
    {
        frames.push_back(transport::frameOfTick(*table, static_cast<double>(attack.tick)));
    }

    // A moment is moved, or followed by a stray key, only where its
    // neighbours are far enough away that the keyboard cannot run the two
    // together into one chord.
    const transport::Frame far = 5 * offset;
    const auto gapAfter = [&frames, far](std::size_t index)
    { return index + 1 < frames.size() ? frames[index + 1] - frames[index] : 4 * far; };
    const auto gapBefore = [&frames, far](std::size_t index)
    { return index > 0 ? frames[index] - frames[index - 1] : 4 * far; };

    for (std::size_t index = 0; index < attacks.size(); ++index)
    {
        const evaluation::ExpectedAttack& attack = attacks[index];
        transport::Frame frame = frames[index];

        // Missed: nothing played.
        if (index % 11 == 6)
        {
            continue;
        }
        const bool spaced = gapBefore(index) >= far && gapAfter(index) >= far;
        if (spaced && index % 5 == 3)
        {
            frame += offset;
        }
        else if (spaced && index % 9 == 4)
        {
            frame -= offset;
        }
        for (std::size_t note = 0; note < attack.notes.size(); ++note)
        {
            // Wrong: a tone above the moment's first note.
            const int written = attack.notes[note].midiNoteNumber;
            press(frame, index % 7 == 2 && note == 0 ? written + 2 : written);
        }

        // Extra: a stray key midway to the next moment.
        if (index % 13 == 8 && gapAfter(index) >= 2 * far && index + 1 < frames.size())
        {
            press(frames[index] + (gapAfter(index) / 2), attack.notes.front().midiNoteNumber + 12);
        }
    }

    // Stopped at the start of the bar after the last one played.
    const score::Part& played = source->parts[part];
    const score::Tick end = static_cast<std::size_t>(bars) < played.measures.size()
                                ? played.measures[static_cast<std::size_t>(bars)].start
                                : score::totalLength(*source);
    const transport::Frame lastFrame = transport::frameOfTick(*table, static_cast<double>(end));

    result.score = std::move(source);
    result.lastFrame = lastFrame;
    result.stoppedInBar = bars - 1;
    result.lifecycle.apply(take::TakeEvent::countInStarted);
    result.lifecycle.apply(take::TakeEvent::performanceStarted);
    result.lifecycle.apply(take::TakeEvent::stopped);
    return result;
}
} // namespace testcontrol
