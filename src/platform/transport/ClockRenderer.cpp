#include "ClockRenderer.h"

#include <algorithm>
#include <cmath>

namespace transport
{
namespace
{
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint8_t>::is_always_lock_free);

// No block holds more clicks than this, whatever the tempo. At 200% of a
// 300 bpm score in 16ths a click is ~1,100 frames apart, so even an 8,192-frame
// block holds eight. The bound is what keeps the loop below bounded for any
// table at all.
constexpr int maximumClicksPerSpan = 64;

// One pass per count-in, per loop wrap, per end: a block crosses few of them.
constexpr int maximumSpansPerBlock = 64;

// A decaying sine with a short attack, so the click is crisp without the
// discontinuity of starting at full amplitude.
void synthesiseClick(float* destination, std::size_t frames, double frequency, double rate)
{
    constexpr double twoPi = 6.283185307179586;
    constexpr double attackSeconds = 0.0005;
    constexpr double decaySeconds = 0.007;
    for (std::size_t index = 0; index < frames; ++index)
    {
        const double seconds = static_cast<double>(index) / rate;
        const double attack = std::min(1.0, seconds / attackSeconds);
        const double envelope = attack * std::exp(-seconds / decaySeconds);
        destination[index] = static_cast<float>(envelope * std::sin(twoPi * frequency * seconds));
    }
}
} // namespace

ClockRenderer::ClockRenderer() : slots(std::make_unique<std::array<Slot, 2>>())
{
    for (auto& slot : *slots)
    {
        synthesise(slot, 44100.0);
    }
}

ClockRenderer::~ClockRenderer() = default;

void ClockRenderer::synthesise(Slot& slot, double sampleRate)
{
    const double rate = std::clamp(sampleRate, 8000.0, maximumSampleRate);
    if (slot.clickRate == rate)
    {
        return;
    }
    slot.clickFrames =
        std::min(maximumClickFrames, static_cast<std::size_t>(std::lround(clickSeconds * rate)));
    synthesiseClick(slot.accented.data(), slot.clickFrames, accentedClickHz, rate);
    synthesiseClick(slot.normal.data(), slot.clickFrames, normalClickHz, rate);
    slot.clickRate = rate;
}

bool ClockRenderer::publish(const ClockTable& table)
{
    const std::uint64_t generation = published.load(std::memory_order_relaxed);
    if (acknowledged.load(std::memory_order_acquire) != generation)
    {
        return false;
    }

    const std::uint64_t next = generation + 1;
    Slot& slot = (*slots)[next & 1U];
    slot.table = table;
    synthesise(slot, table.sampleRate);
    published.store(next, std::memory_order_release);
    return true;
}

bool ClockRenderer::publishAcknowledged() const noexcept
{
    return acknowledged.load(std::memory_order_acquire) ==
           published.load(std::memory_order_relaxed);
}

std::uint64_t ClockRenderer::publishedGeneration() const noexcept
{
    return published.load(std::memory_order_relaxed);
}

const ClockTable* ClockRenderer::tableForGeneration(std::uint64_t generation) const noexcept
{
    const std::uint64_t latest = published.load(std::memory_order_relaxed);
    if (generation == 0 || generation > latest || generation + 1 < latest)
    {
        return nullptr;
    }
    return &(*slots)[generation & 1U].table;
}

bool ClockRenderer::send(const ClockCommand& command) noexcept
{
    return commands.push(command);
}

void ClockRenderer::setMetronomeEnabled(bool enabled) noexcept
{
    metronomeOn.store(enabled, std::memory_order_relaxed);
}

void ClockRenderer::setVolume(float newGain) noexcept
{
    gain.store(std::clamp(newGain, 0.0f, 1.0f), std::memory_order_relaxed);
}

void ClockRenderer::setAccentEnabled(bool enabled) noexcept
{
    accentOn.store(enabled, std::memory_order_relaxed);
}

bool ClockRenderer::metronomeEnabled() const noexcept
{
    return metronomeOn.load(std::memory_order_relaxed);
}

float ClockRenderer::volume() const noexcept
{
    return gain.load(std::memory_order_relaxed);
}

bool ClockRenderer::accentEnabled() const noexcept
{
    return accentOn.load(std::memory_order_relaxed);
}

ClockSnapshot ClockRenderer::snapshot() const noexcept
{
    ClockSnapshot result;
    for (;;)
    {
        const std::uint32_t before = snapshotSequence.load(std::memory_order_acquire);
        if ((before & 1U) != 0)
        {
            continue;
        }
        result.position = snapshotPosition.load(std::memory_order_relaxed);
        result.state = static_cast<ClockState>(snapshotState.load(std::memory_order_relaxed));
        result.countingIn = snapshotCountingIn.load(std::memory_order_relaxed);
        result.countInTarget = snapshotCountInTarget.load(std::memory_order_relaxed);
        result.generation = snapshotGeneration.load(std::memory_order_relaxed);
        result.streamFrame = snapshotStreamFrame.load(std::memory_order_relaxed);
        result.blockStartPosition = snapshotBlockStart.load(std::memory_order_relaxed);
        result.blockTimeMilliseconds = snapshotBlockTime.load(std::memory_order_relaxed);
        result.sampleRate = snapshotSampleRate.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (snapshotSequence.load(std::memory_order_relaxed) == before)
        {
            return result;
        }
    }
}

ClockEventCounts ClockRenderer::events() const noexcept
{
    return {
        seekCount.load(std::memory_order_acquire), wrapCount.load(std::memory_order_acquire),
        endCount.load(std::memory_order_acquire)};
}

const ClockRenderer::Slot* ClockRenderer::activeSlot() const noexcept PRACTICE_TAKES_NONBLOCKING
{
    return current == 0 ? nullptr : &(*slots)[current & 1U];
}

void ClockRenderer::pickUpTable() noexcept PRACTICE_TAKES_NONBLOCKING
{
    const std::uint64_t latest = published.load(std::memory_order_acquire);
    if (latest == current)
    {
        return;
    }

    const Slot& next = (*slots)[latest & 1U];
    if (const Slot* previous = activeSlot())
    {
        // The same place in the score under the new table, so a tempo-scale
        // change continues from the beat it was on rather than jumping.
        if (countingIn)
        {
            const double beatsLeft =
                countIn.framesPerBeat > 0.0
                    ? static_cast<double>(countIn.targetFrame - position) / countIn.framesPerBeat
                    : 0.0;
            const double targetTick = tickAtFrame(previous->table, countIn.targetFrame);
            const int bars = countIn.beatsPerBar > 0 ? countIn.beats / countIn.beatsPerBar : 0;
            countIn = countInBefore(
                next.table, static_cast<Tick>(std::floor(targetTick + 0.5)), std::max(1, bars));
            countIn.targetFrame = frameOfTick(next.table, targetTick);
            countIn.startFrame =
                countIn.targetFrame -
                roundToFrame(static_cast<double>(countIn.beats) * countIn.framesPerBeat);
            position = countIn.targetFrame - roundToFrame(beatsLeft * countIn.framesPerBeat);
        }
        else
        {
            position = reanchor(previous->table, next.table, position);
        }
    }

    current = latest;
    acknowledged.store(latest, std::memory_order_release);
}

void ClockRenderer::startCountIn(int bars) noexcept PRACTICE_TAKES_NONBLOCKING
{
    countingIn = false;
    const Slot* slot = activeSlot();
    if (slot == nullptr || bars <= 0)
    {
        return;
    }
    const ClockTable& table = slot->table;
    const double tick = tickAtFrame(table, position);
    countIn = countInBefore(table, static_cast<Tick>(std::floor(tick + 0.5)), bars);
    countIn.targetFrame = position;
    countIn.startFrame =
        position - roundToFrame(static_cast<double>(countIn.beats) * countIn.framesPerBeat);
    if (countIn.beats > 0 && countIn.startFrame < countIn.targetFrame)
    {
        countingIn = true;
        position = countIn.startFrame;
    }
}

void ClockRenderer::applyCommands() noexcept PRACTICE_TAKES_NONBLOCKING
{
    const Slot* slot = activeSlot();
    ClockCommand command;
    while (commands.pop(command))
    {
        switch (command.kind)
        {
        case ClockCommand::Kind::play:
            if (state != ClockState::playing)
            {
                state = ClockState::playing;
                startCountIn(command.countInBars);
            }
            break;
        case ClockCommand::Kind::pause:
            if (countingIn)
            {
                // A count-in paused is a count-in abandoned: resuming counts in
                // again from the point it was counting in to.
                position = countIn.targetFrame;
                countingIn = false;
            }
            if (state == ClockState::playing)
            {
                state = ClockState::paused;
            }
            break;
        case ClockCommand::Kind::stop:
            state = ClockState::stopped;
            countingIn = false;
            position = slot != nullptr && slot->table.loopEnabled ? slot->table.loopStartFrame : 0;
            break;
        case ClockCommand::Kind::seek:
            countingIn = false;
            position =
                slot != nullptr ? frameOfTick(slot->table, static_cast<double>(command.tick)) : 0;
            seekCount.fetch_add(1, std::memory_order_release);
            break;
        }
    }
}

void ClockRenderer::mixVoiceUntil(RenderBlock& block, int offset) noexcept
    PRACTICE_TAKES_NONBLOCKING
{
    const int until = std::clamp(offset, 0, block.frames);
    const Slot* slot = activeSlot();
    if (voice.sounding && slot != nullptr && until > mixedUpTo)
    {
        const float* click = voice.accented ? slot->accented.data() : slot->normal.data();
        const float level = gain.load(std::memory_order_relaxed);
        const std::size_t available =
            slot->clickFrames > voice.readPosition ? slot->clickFrames - voice.readPosition : 0;
        const int count = static_cast<int>(
            std::min<std::size_t>(available, static_cast<std::size_t>(until - mixedUpTo)));

        for (int channel = 0; channel < block.outputChannels; ++channel)
        {
            float* output = block.outputs != nullptr ? block.outputs[channel] : nullptr;
            if (output == nullptr)
            {
                continue;
            }
            for (int index = 0; index < count; ++index)
            {
                output[mixedUpTo + index] +=
                    click[voice.readPosition + static_cast<std::size_t>(index)] * level;
            }
        }

        voice.readPosition += static_cast<std::size_t>(count);
        if (voice.readPosition >= slot->clickFrames)
        {
            voice.sounding = false;
        }
    }
    mixedUpTo = std::max(mixedUpTo, until);
}

void ClockRenderer::startClick(RenderBlock& block, int offset, bool accented) noexcept
    PRACTICE_TAKES_NONBLOCKING
{
    mixVoiceUntil(block, offset);
    voice.sounding = true;
    voice.accented = accented && accentOn.load(std::memory_order_relaxed);
    voice.readPosition = 0;
}

void ClockRenderer::advance(RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING
{
    const Slot* slot = activeSlot();
    if (slot == nullptr || state != ClockState::playing || block.frames <= 0)
    {
        return;
    }

    const ClockTable& table = slot->table;
    const bool metronome = metronomeOn.load(std::memory_order_relaxed);
    Frame remaining = block.frames;
    int offset = 0;

    for (int span = 0; remaining > 0 && span < maximumSpansPerBlock; ++span)
    {
        if (countingIn)
        {
            // The count-in always sounds: it is how a person knows when to
            // start, whether or not they want the metronome after it.
            const Frame length = std::min(remaining, countIn.targetFrame - position);
            Frame cursor = position;
            Click click;
            for (int clicks = 0;
                 clicks < maximumClicksPerSpan && nextCountInClick(countIn, cursor, click) &&
                 click.frame < position + length;
                 ++clicks)
            {
                startClick(
                    block, offset + static_cast<int>(click.frame - position), click.accented);
                cursor = click.frame + 1;
            }
            position += length;
            offset += static_cast<int>(length);
            remaining -= length;
            if (position >= countIn.targetFrame)
            {
                countingIn = false;
            }
            continue;
        }

        const bool looping = table.loopEnabled && position < table.loopEndFrame;
        if (!looping && position >= table.endFrame)
        {
            // The end of the piece: stop, as the stop command would.
            state = ClockState::stopped;
            position = table.loopEnabled ? table.loopStartFrame : 0;
            endCount.fetch_add(1, std::memory_order_release);
            return;
        }

        const Frame boundary = looping ? table.loopEndFrame : table.endFrame;
        const Frame length = std::min(remaining, boundary - position);

        if (metronome)
        {
            Frame cursor = position;
            Click click;
            for (int clicks = 0; clicks < maximumClicksPerSpan &&
                                 clickStartsIn(table, cursor, position + length - cursor, click);
                 ++clicks)
            {
                startClick(
                    block, offset + static_cast<int>(click.frame - position), click.accented);
                cursor = click.frame + 1;
            }
        }

        position += length;
        offset += static_cast<int>(length);
        remaining -= length;

        if (looping && position >= table.loopEndFrame)
        {
            if (wrapIntoLoop(table, position) > 0)
            {
                wrapCount.fetch_add(1, std::memory_order_release);
            }
        }
    }
}

void ClockRenderer::publishSnapshot(
    Frame blockStartPosition,
    std::uint64_t blockStartStream,
    const RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING
{
    const std::uint32_t sequence = snapshotSequence.load(std::memory_order_relaxed);
    snapshotSequence.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    snapshotPosition.store(position, std::memory_order_relaxed);
    snapshotState.store(static_cast<std::uint8_t>(state), std::memory_order_relaxed);
    snapshotCountingIn.store(countingIn, std::memory_order_relaxed);
    snapshotCountInTarget.store(countIn.targetFrame, std::memory_order_relaxed);
    snapshotGeneration.store(current, std::memory_order_relaxed);
    snapshotStreamFrame.store(blockStartStream, std::memory_order_relaxed);
    snapshotBlockStart.store(blockStartPosition, std::memory_order_relaxed);
    snapshotBlockTime.store(block.timeMilliseconds, std::memory_order_relaxed);
    snapshotSampleRate.store(block.sampleRate, std::memory_order_relaxed);

    snapshotSequence.store(sequence + 2, std::memory_order_release);
}

void ClockRenderer::render(RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING
{
    pickUpTable();
    applyCommands();

    const Frame blockStartPosition = position;
    const std::uint64_t blockStartStream = streamFrames;
    mixedUpTo = 0;

    advance(block);
    mixVoiceUntil(block, block.frames);

    streamFrames = blockStartStream + static_cast<std::uint64_t>(std::max(0, block.frames));
    publishSnapshot(blockStartPosition, blockStartStream, block);
}
} // namespace transport
