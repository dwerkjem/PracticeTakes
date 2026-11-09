#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../../platform/evaluation/ExpectedAttacks.h"
#include "../../platform/take/Take.h"
#include "../../platform/transport/ClockMath.h"

// A keyboard or sung take of an open score with known mistakes, for the
// capture harness to review (no JUCE dependency).
// openspec/changes/archive/2026-11-08-take-review/design.md, decision 5.
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

// What `syntheticSungTake` sings at each attack, on a fixed pattern over the
// part's attacks (openspec/changes/archive/2026-11-09-sung-pitch-judging/design.md, decision 9).
enum class SungNote
{
    inTune,
    sharp,
    flat,
    wrong,
    unsure,
    missed
};

[[nodiscard]] inline SungNote sungNoteAt(std::size_t index) noexcept
{
    if (index % 11 == 6)
    {
        return SungNote::missed;
    }
    if (index % 13 == 9)
    {
        return SungNote::wrong;
    }
    if (index % 17 == 12)
    {
        return SungNote::unsure;
    }
    if (index % 5 == 1)
    {
        return SungNote::sharp;
    }
    if (index % 7 == 3)
    {
        return SungNote::flat;
    }
    return SungNote::inTune;
}

inline constexpr double syntheticSharpCents = 35.0;
inline constexpr double syntheticWrongCents = 200.0;

// A microphone take of a part sung through `bars` bars, at tempo, at A4 =
// `referenceHz`: a voice-like tone for each attack's first note, scooping up
// into it, with a breath before the next. Some notes are sharp, flat, wrong,
// too noisy to be sure of, or not sung, by `sungNoteAt`.
[[nodiscard]] inline take::Take syntheticSungTake(
    std::shared_ptr<const score::Score> source,
    std::size_t part,
    std::int64_t bars,
    double sampleRate = 44100.0,
    double referenceHz = 440.0)
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
    result.referenceHz = referenceHz;
    result.scoreTitle = source->metadata.workTitle;
    result.usedMicrophone = true;

    std::vector<evaluation::ExpectedAttack> attacks = evaluation::expectedAttacksOf(*source, part);
    attacks.erase(
        std::find_if(
            attacks.begin(), attacks.end(), [bars](const evaluation::ExpectedAttack& attack)
            { return static_cast<std::int64_t>(attack.measureIndex) >= bars; }),
        attacks.end());

    const score::Part& sung = source->parts[part];
    const score::Tick end = static_cast<std::size_t>(bars) < sung.measures.size()
                                ? sung.measures[static_cast<std::size_t>(bars)].start
                                : score::totalLength(*source);
    const transport::Frame lastFrame = transport::frameOfTick(*table, static_cast<double>(end));

    // Half a second of silence before the first note, as a count-in leaves:
    // an onset needs something before it to stand out from.
    const auto lead = static_cast<transport::Frame>(0.5 * sampleRate);
    result.audioStart = -lead;
    result.countInFrames = lead;
    result.audio.assign(
        static_cast<std::size_t>(std::max<transport::Frame>(0, lastFrame) + lead), 0.0f);

    constexpr double twoPi = 6.283185307179586;
    const auto breath = static_cast<transport::Frame>(0.04 * sampleRate);
    const auto scoop = static_cast<transport::Frame>(0.06 * sampleRate);
    const auto ramp = static_cast<transport::Frame>(0.01 * sampleRate);
    std::uint32_t noise = 12345;

    for (std::size_t index = 0; index < attacks.size(); ++index)
    {
        const SungNote kind = sungNoteAt(index);
        if (kind == SungNote::missed)
        {
            continue;
        }
        const transport::Frame from =
            transport::frameOfTick(*table, static_cast<double>(attacks[index].tick));
        transport::Frame to =
            index + 1 < attacks.size()
                ? transport::frameOfTick(*table, static_cast<double>(attacks[index + 1].tick))
                : lastFrame;
        to = std::min<transport::Frame>(to - breath, result.audioEnd());
        if (to - from < 2 * ramp)
        {
            continue;
        }

        double cents = 0.0;
        switch (kind)
        {
        case SungNote::sharp:
            cents = syntheticSharpCents;
            break;
        case SungNote::flat:
            cents = -syntheticSharpCents;
            break;
        case SungNote::wrong:
            cents = syntheticWrongCents;
            break;
        default:
            break;
        }
        const double midi =
            static_cast<double>(attacks[index].notes.front().midiNoteNumber) + (cents / 100.0);

        double phase = 0.0;
        for (transport::Frame frame = from; frame < to; ++frame)
        {
            const transport::Frame into = frame - from;
            const double level = 0.3 * std::min(
                                           1.0, static_cast<double>(std::min(into, to - frame)) /
                                                    static_cast<double>(ramp));
            float sample = 0.0f;
            if (kind == SungNote::unsure)
            {
                noise = (noise * 1664525U) + 1013904223U;
                sample = static_cast<float>(
                    level * ((static_cast<double>(noise >> 8U) / 8388608.0) - 1.0));
            }
            else
            {
                // A scoop up from 60 cents below over the first 60 ms.
                const double bend =
                    into < scoop
                        ? -0.6 * (1.0 - (static_cast<double>(into) / static_cast<double>(scoop)))
                        : 0.0;
                const double hertz = referenceHz * std::pow(2.0, (midi + bend - 69.0) / 12.0);
                phase += twoPi * hertz / sampleRate;
                sample = static_cast<float>(
                    level * ((0.5 * std::sin(phase)) + (0.35 * std::sin(2.0 * phase)) +
                             (0.2 * std::sin(3.0 * phase)) + (0.1 * std::sin(4.0 * phase))));
            }
            result.audio[static_cast<std::size_t>(frame + lead)] = sample;
        }
    }

    result.score = std::move(source);
    result.lastFrame = lastFrame;
    result.stoppedInBar = bars - 1;
    result.lifecycle.apply(take::TakeEvent::countInStarted);
    result.lifecycle.apply(take::TakeEvent::performanceStarted);
    result.lifecycle.apply(take::TakeEvent::stopped);
    return result;
}
} // namespace testcontrol
