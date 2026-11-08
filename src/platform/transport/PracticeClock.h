#pragma once

#include <juce_events/juce_events.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "../score/CurrentScore.h"
#include "ClockMath.h"
#include "ClockRenderer.h"

// The practice clock: one per application, shared by every tool through
// ToolServices (openspec/changes/archive/2026-11-08-practice-clock).
//
// This is the message-thread half. It owns the `ClockRenderer` that the audio
// callback runs, and is the only thing that talks to it: it builds the segment
// table from the current score, publishes it again when the score, the tempo
// scale, the loop or the device's sample rate changes, and turns the
// renderer's snapshot into a `ClockPosition` for its listeners 60 times a
// second.
//
// The renderer is handed to the audio service through `attach` at construction
// and taken back at destruction, which waits for any callback still running it.
// MainComponent declares this after the audio service and the score holder and
// before its live tools, so every tool is gone before the clock, and the clock
// before the service.
namespace transport
{
// Where the clock is, in every unit a tool might want.
struct ClockPosition
{
    // Score ticks, fractional. Negative while counting in to the start.
    double tick = 0.0;

    // Seconds into the score at its written tempo -- the tempo map's seconds,
    // not wall-clock time at the current scale.
    double seconds = 0.0;

    // Frames of the audio callback, at the current scale.
    Frame frame = 0;

    // Zero-based bar and beat. During a count-in the bar is negative: -1 is
    // the bar before the start point.
    std::int64_t bar = 0;
    int beat = 0;

    ClockState state = ClockState::stopped;
    bool countingIn = false;
    int scalePercent = defaultScalePercent;
    bool hasScore = false;
};

// One notification: the position, and the discrete events since the last one,
// each counted once.
struct ClockUpdate
{
    ClockPosition position;
    int seeks = 0;
    int wraps = 0;
    bool reachedEnd = false;
};

class ClockListener
{
  public:
    virtual ~ClockListener() = default;
    virtual void clockChanged(const ClockUpdate& update) = 0;
};

class PracticeClock final : private juce::Timer
{
  public:
    // How a renderer reaches the audio callback: AudioInputService's
    // `setRealtimeRenderer`, or a test's stand-in. Called with null to detach.
    using AttachRenderer = std::function<void(RealtimeRenderer*)>;

    static constexpr int notificationRateHz = 60;

    PracticeClock(AttachRenderer attach, const score::CurrentScore& currentScore);
    ~PracticeClock() override;

    PracticeClock(const PracticeClock&) = delete;
    PracticeClock& operator=(const PracticeClock&) = delete;

    // Start, counting in first when the count-in is not zero bars.
    void play();
    void pause();
    void togglePlay();
    // Back to the start of the loop, or of the score.
    void stop();
    void seek(Tick tick);
    // The first tick of a zero-based bar.
    void seekToBar(std::int64_t bar);

    // 5%-200% of the score's tempo, in 5% steps. Takes effect without a jump.
    void setScalePercent(int percent);
    [[nodiscard]] int scalePercent() const noexcept;

    // The loop. With no region set, looping plays the whole score round.
    void setLoopEnabled(bool enabled);
    [[nodiscard]] bool loopEnabled() const noexcept;
    void setLoopRegion(Tick start, Tick end);
    void clearLoopRegion();

    void setCountInBars(int bars);
    [[nodiscard]] int countInBars() const noexcept;

    void setMetronomeEnabled(bool enabled);
    [[nodiscard]] bool metronomeEnabled() const noexcept;
    // 0-1.
    void setMetronomeVolume(float volume);
    [[nodiscard]] float metronomeVolume() const noexcept;
    void setAccentEnabled(bool enabled);
    [[nodiscard]] bool accentEnabled() const noexcept;

    [[nodiscard]] ClockPosition position() const;
    [[nodiscard]] ClockTiming timing() const noexcept;
    [[nodiscard]] bool hasScore() const noexcept;

    // Whether the score had more tempo and meter changes than the clock holds.
    [[nodiscard]] bool truncated() const noexcept;

    void addListener(ClockListener* listener);
    void removeListener(ClockListener* listener);

    // One notification tick. The timer calls it; a test calls it directly,
    // between driving blocks through `renderer()`.
    void poll();

    // For tests: the renderer, to drive blocks through without a device.
    [[nodiscard]] ClockRenderer& renderer() noexcept;

  private:
    void timerCallback() override;

    // Rebuild the table if anything it depends on changed, and try to publish
    // it. A publish that has to wait for a block is retried on the next tick.
    void refreshTable();
    void send(const ClockCommand& command);

    [[nodiscard]] ClockPosition positionFrom(const ClockSnapshot& snapshot) const;

    AttachRenderer attachRenderer;
    const score::CurrentScore& scoreHolder;
    std::unique_ptr<ClockRenderer> clockRenderer;

    // The score the loop and position were last reset for.
    std::shared_ptr<const score::Score> seenScore;

    // What the published table was built from.
    std::shared_ptr<const score::Score> builtScore;
    int builtScalePercent = 0;
    double builtSampleRate = 0.0;
    bool builtLoopEnabled = false;
    Tick builtLoopStart = 0;
    Tick builtLoopEnd = 0;
    bool tablePending = true;
    std::unique_ptr<ClockTable> pendingTable = std::make_unique<ClockTable>();

    int scale = defaultScalePercent;
    bool looping = false;
    Tick loopStart = 0;
    Tick loopEnd = 0;
    int countIn = defaultCountInBars;

    // Commands the queue had no room for, sent again on the next tick.
    std::vector<ClockCommand> unsent;

    std::vector<ClockListener*> listeners;
    ClockEventCounts lastEvents;
    ClockPosition lastNotified;
    bool notifiedOnce = false;
};
} // namespace transport
