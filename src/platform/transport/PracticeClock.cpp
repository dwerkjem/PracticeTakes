#include "PracticeClock.h"

#include <algorithm>
#include <cmath>

namespace transport
{
PracticeClock::PracticeClock(AttachRenderer attach, const score::CurrentScore& currentScore)
    : attachRenderer(std::move(attach)), scoreHolder(currentScore),
      clockRenderer(std::make_unique<ClockRenderer>())
{
    refreshTable();
    if (attachRenderer)
    {
        attachRenderer(clockRenderer.get());
    }
    startTimerHz(notificationRateHz);
}

PracticeClock::~PracticeClock()
{
    stopTimer();

    // Waits for any callback still inside the renderer, so it is safe to
    // destroy once this returns.
    if (attachRenderer)
    {
        attachRenderer(nullptr);
    }
}

ClockRenderer& PracticeClock::renderer() noexcept
{
    return *clockRenderer;
}

void PracticeClock::timerCallback()
{
    poll();
}

void PracticeClock::send(const ClockCommand& command)
{
    // Behind a table that could not be published yet, a command waits, so a
    // seek after opening a score is converted with the new score's table.
    if (tablePending || !unsent.empty() || !clockRenderer->send(command))
    {
        unsent.push_back(command);
    }
}

void PracticeClock::play()
{
    if (!hasScore())
    {
        return;
    }
    send({ClockCommand::Kind::play, 0, countIn});
}

void PracticeClock::pause()
{
    send({ClockCommand::Kind::pause});
}

void PracticeClock::togglePlay()
{
    if (clockRenderer->snapshot().state == ClockState::playing)
    {
        pause();
    }
    else
    {
        play();
    }
}

void PracticeClock::stop()
{
    send({ClockCommand::Kind::stop});
}

void PracticeClock::seek(Tick tick)
{
    if (locked)
    {
        return;
    }

    send({ClockCommand::Kind::seek, std::max<Tick>(0, tick)});
}

void PracticeClock::seekToBar(std::int64_t bar)
{
    if (locked)
    {
        return;
    }

    const auto current = scoreHolder.score();
    if (current == nullptr || current->parts.empty() || current->parts.front().measures.empty())
    {
        seek(0);
        return;
    }
    const auto& measures = current->parts.front().measures;
    const auto index = static_cast<std::size_t>(
        std::clamp<std::int64_t>(bar, 0, static_cast<std::int64_t>(measures.size()) - 1));
    seek(measures[index].start);
}

void PracticeClock::setScalePercent(int percent)
{
    if (locked)
    {
        return;
    }

    scale = clampScalePercent(percent);
    refreshTable();
}

int PracticeClock::scalePercent() const noexcept
{
    return scale;
}

void PracticeClock::setLoopEnabled(bool enabled)
{
    if (locked)
    {
        return;
    }

    looping = enabled;
    refreshTable();
}

bool PracticeClock::loopEnabled() const noexcept
{
    return looping;
}

void PracticeClock::setLoopRegion(Tick start, Tick end)
{
    if (locked)
    {
        return;
    }

    loopStart = std::max<Tick>(0, std::min(start, end));
    loopEnd = std::max<Tick>(0, std::max(start, end));
    refreshTable();
}

void PracticeClock::clearLoopRegion()
{
    if (locked)
    {
        return;
    }

    loopStart = 0;
    loopEnd = 0;
    refreshTable();
}

void PracticeClock::setCountInBars(int bars)
{
    countIn = clampCountInBars(bars);
}

int PracticeClock::countInBars() const noexcept
{
    return countIn;
}

void PracticeClock::setMetronomeEnabled(bool enabled)
{
    clockRenderer->setMetronomeEnabled(enabled);
}

bool PracticeClock::metronomeEnabled() const noexcept
{
    return clockRenderer->metronomeEnabled();
}

void PracticeClock::setMetronomeVolume(float volume)
{
    clockRenderer->setVolume(volume);
}

float PracticeClock::metronomeVolume() const noexcept
{
    return clockRenderer->volume();
}

void PracticeClock::setAccentEnabled(bool enabled)
{
    clockRenderer->setAccentEnabled(enabled);
}

bool PracticeClock::accentEnabled() const noexcept
{
    return clockRenderer->accentEnabled();
}

bool PracticeClock::hasScore() const noexcept
{
    return builtScore != nullptr;
}

void PracticeClock::lock() noexcept
{
    locked = true;
}

void PracticeClock::unlock() noexcept
{
    locked = false;
}

bool PracticeClock::isLocked() const noexcept
{
    return locked;
}

std::shared_ptr<const ClockTable> PracticeClock::currentTable() const
{
    const auto* table = clockRenderer->tableForGeneration(clockRenderer->publishedGeneration());
    return table != nullptr ? std::make_shared<const ClockTable>(*table) : nullptr;
}

bool PracticeClock::truncated() const noexcept
{
    const auto* table = clockRenderer->tableForGeneration(clockRenderer->publishedGeneration());
    return table != nullptr && table->truncated;
}

void PracticeClock::refreshTable()
{
    const auto current = scoreHolder.score();
    const auto snapshotRate = clockRenderer->snapshot().sampleRate;
    const double rate =
        snapshotRate > 0.0 ? snapshotRate : (builtSampleRate > 0.0 ? builtSampleRate : 44100.0);

    if (current != seenScore)
    {
        seenScore = current;
        // A different score: the old loop and position mean nothing in it.
        // Stopped before the new table is published, so the stop lands on the
        // old one and the new one starts from its beginning.
        looping = false;
        loopStart = 0;
        loopEnd = 0;
        if (builtScore != nullptr)
        {
            clockRenderer->send({ClockCommand::Kind::stop});
        }
    }

    const Tick length = current != nullptr ? score::totalLength(*current) : 0;
    const bool hasRegion = loopEnd > loopStart;
    const Tick wantedLoopStart = looping ? (hasRegion ? loopStart : 0) : 0;
    const Tick wantedLoopEnd = looping ? (hasRegion ? loopEnd : length) : 0;

    const bool unchanged = current == builtScore && scale == builtScalePercent &&
                           rate == builtSampleRate && looping == builtLoopEnabled &&
                           wantedLoopStart == builtLoopStart && wantedLoopEnd == builtLoopEnd;
    if (unchanged && !tablePending)
    {
        return;
    }

    if (current != nullptr)
    {
        buildTable(*pendingTable, *current, scale, rate, wantedLoopStart, wantedLoopEnd);
    }
    else
    {
        *pendingTable = ClockTable{};
        pendingTable->sampleRate = rate;
        pendingTable->scalePercent = scale;
    }

    if (!clockRenderer->publish(*pendingTable))
    {
        tablePending = true;
        return;
    }

    tablePending = false;
    builtScore = current;
    builtScalePercent = scale;
    builtSampleRate = rate;
    builtLoopEnabled = looping;
    builtLoopStart = wantedLoopStart;
    builtLoopEnd = wantedLoopEnd;

    // Whatever waited behind the table goes now, in order.
    std::size_t sent = 0;
    while (sent < unsent.size() && clockRenderer->send(unsent[sent]))
    {
        ++sent;
    }
    unsent.erase(unsent.begin(), unsent.begin() + static_cast<std::ptrdiff_t>(sent));
}

ClockTiming PracticeClock::timing() const noexcept
{
    const auto snapshot = clockRenderer->snapshot();
    return {
        snapshot.streamFrame, snapshot.blockStartPosition, snapshot.blockTimeMilliseconds,
        snapshot.sampleRate};
}

ClockPosition PracticeClock::position() const
{
    return positionFrom(clockRenderer->snapshot());
}

ClockPosition PracticeClock::positionFrom(const ClockSnapshot& snapshot) const
{
    ClockPosition result;
    result.state = snapshot.state;
    result.countingIn = snapshot.countingIn;
    result.frame = snapshot.position;
    result.hasScore = builtScore != nullptr;

    const ClockTable* table = clockRenderer->tableForGeneration(snapshot.generation);
    if (table == nullptr || table->empty())
    {
        result.scalePercent = scale;
        return result;
    }
    result.scalePercent = table->scalePercent;

    if (snapshot.countingIn)
    {
        const double targetTick = tickAtFrame(*table, snapshot.countInTarget);
        const ClockSegment& segment = table->segments[segmentForTick(*table, targetTick)];
        const Frame before = snapshot.countInTarget - snapshot.position;
        result.tick = targetTick - static_cast<double>(before) / segment.framesPerTick;

        // Which count-in beat is sounding: 1 is the last before the target.
        const double framesPerBeat =
            static_cast<double>(segment.ticksPerBeat) * segment.framesPerTick;
        const auto fromEnd = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(std::ceil(static_cast<double>(before) / framesPerBeat)));
        const std::int64_t perBar = std::max(1, segment.beatsPerBar);
        result.bar = -1 - (fromEnd - 1) / perBar;
        result.beat = static_cast<int>(perBar - 1 - (fromEnd - 1) % perBar);
    }
    else
    {
        result.tick = tickAtFrame(*table, snapshot.position);
        const BeatPosition beat = beatAtFrame(*table, snapshot.position);
        result.bar = beat.bar;
        result.beat = beat.beat;
    }

    if (builtScore != nullptr)
    {
        const double whole = std::floor(result.tick);
        const auto tick = static_cast<Tick>(whole);
        result.seconds =
            builtScore->tempoMap.tickToSeconds(tick) +
            (result.tick - whole) *
                score::ticksToSecondsAt(1, builtScore->tempoMap.beatsPerMinuteAt(tick));
    }
    return result;
}

void PracticeClock::addListener(ClockListener* listener)
{
    if (listener != nullptr &&
        std::find(listeners.begin(), listeners.end(), listener) == listeners.end())
    {
        listeners.push_back(listener);
    }
}

void PracticeClock::removeListener(ClockListener* listener)
{
    listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
}

void PracticeClock::poll()
{
    refreshTable();

    const auto snapshot = clockRenderer->snapshot();
    const auto events = clockRenderer->events();

    ClockUpdate update;
    update.position = positionFrom(snapshot);
    update.seeks = static_cast<int>(events.seeks - lastEvents.seeks);
    update.wraps = static_cast<int>(events.wraps - lastEvents.wraps);
    update.reachedEnd = events.ends != lastEvents.ends;
    lastEvents = events;

    const auto& now = update.position;
    const bool same =
        notifiedOnce && now.frame == lastNotified.frame && now.state == lastNotified.state &&
        now.countingIn == lastNotified.countingIn &&
        now.scalePercent == lastNotified.scalePercent && now.hasScore == lastNotified.hasScore;
    if (same && update.seeks == 0 && update.wraps == 0 && !update.reachedEnd)
    {
        return;
    }

    notifiedOnce = true;
    lastNotified = now;

    // A copy, so a listener may remove itself from inside the callback.
    const auto toNotify = listeners;
    for (auto* listener : toNotify)
    {
        if (std::find(listeners.begin(), listeners.end(), listener) != listeners.end())
        {
            listener->clockChanged(update);
        }
    }
}
} // namespace transport
