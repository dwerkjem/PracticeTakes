#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "../audio/AudioSampleFifo.h"
#include "../audio/RealtimeRenderer.h"
#include "../audio/RealtimeSafety.h"
#include "ClockCommandQueue.h"
#include "ClockMath.h"
#include "PlaybackEvents.h"

// The practice clock's audio-thread half: it keeps the position, counts in,
// loops, and sounds the metronome, inside the audio callback
// (openspec/changes/archive/2026-11-08-practice-clock, design.md).
//
// No JUCE dependency, so it is driven in tests block by block, and through the
// real callback under RealtimeSanitizer.
//
// What crosses between the threads, and how:
//
//  - **The table** (message -> audio). Two preallocated slots, each a
//    `ClockTable` plus the two clicks synthesised at the table's sample rate.
//    The message thread fills the slot the renderer is not using and publishes
//    its generation with release; the renderer picks it up with acquire at the
//    top of a block and echoes the generation back. The message thread will not
//    refill a slot until that echo arrives, so a callback never reads a table
//    half-written.
//  - **Commands** (message -> audio). An SPSC queue, drained each block.
//  - **Settings** (message -> audio). Atomics: metronome on, volume, accent;
//    and for the score's playback, the mask of tracks that sound and their
//    volume (openspec/changes/archive/2026-11-08-score-playback).
//  - **The score's notes** travel in the slot beside its table, so the two
//    always agree, and sound through a `ScoreSynth` handed over once it has
//    loaded.
//  - **Where it is** (audio -> message). A sequence-locked snapshot written once
//    per block, and monotonic counters for seeks, wraps and the end of the
//    piece, so each is reported once however the display ticks fall.
namespace transport
{
class ScoreSynth;

enum class ClockState : std::uint8_t
{
    stopped,
    playing,
    paused
};

// One block's view of the clock, as the message thread reads it.
struct ClockSnapshot
{
    // The position at the end of the last block, in that block's table.
    Frame position = 0;

    ClockState state = ClockState::stopped;
    bool countingIn = false;

    // Where the count-in ends, while counting in.
    Frame countInTarget = 0;

    // The generation of the table `position` is in.
    std::uint64_t generation = 0;

    // For placing events from another thread on the timeline (#40), all at the
    // instant the last block began: how many frames the renderer had been
    // handed since it was built, whatever the clock was doing; the clock's
    // position; and the high-resolution time.
    std::uint64_t streamFrame = 0;
    Frame blockStartPosition = 0;
    double blockTimeMilliseconds = 0.0;
    double sampleRate = 0.0;
};

// The (frame, time) pair the audio callback last published: for placing an
// event stamped on another thread -- a MIDI note -- on the clock's timeline.
struct ClockTiming
{
    std::uint64_t streamFrame = 0;
    Frame position = 0;
    double timeMilliseconds = 0.0;
    double sampleRate = 0.0;

    // The clock frame at `milliseconds` on the same high-resolution clock, by
    // the elapsed time since the block began. Meaningful while playing.
    [[nodiscard]] Frame frameAt(double milliseconds) const noexcept
    {
        return position + roundToFrame((milliseconds - timeMilliseconds) * sampleRate / 1000.0);
    }
};

// One block the renderer recorded while a take was armed: where on the clock it
// started, how many samples it holds, and whether the clock was counting in.
// Pushed beside the samples, so the message thread can place every sample on
// the timeline exactly (openspec/changes/archive/2026-11-08-takes, design.md decision 1).
struct RecordedBlock
{
    Frame position = 0;
    std::uint32_t frames = 0;
    bool countingIn = false;
};

// Monotonic counts of the discrete events. A reader keeps the last counts it
// saw; the difference is how many of each happened since.
struct ClockEventCounts
{
    std::uint64_t seeks = 0;
    std::uint64_t wraps = 0;
    std::uint64_t ends = 0;
};

class ClockRenderer final : public RealtimeRenderer
{
  public:
    // The longest click, at the highest rate a device will run at.
    static constexpr double clickSeconds = 0.030;
    static constexpr double maximumSampleRate = 192000.0;
    static constexpr std::size_t maximumClickFrames = 5760;

    static constexpr double accentedClickHz = 1500.0;
    static constexpr double normalClickHz = 1000.0;

    // Builds both slots and the clicks at 44.1 kHz, which a publish at another
    // rate re-synthesises. Everything the audio thread will touch is allocated
    // here.
    ClockRenderer();
    ~ClockRenderer() override;

    ClockRenderer(const ClockRenderer&) = delete;
    ClockRenderer& operator=(const ClockRenderer&) = delete;

    // --- Message thread -----------------------------------------------------

    // Publish a new table, and the score's notes on it. False when the
    // previous publish has not yet been picked up by a block, in which case
    // nothing changed and the caller tries again later.
    [[nodiscard]] bool publish(const ClockTable& table);
    [[nodiscard]] bool publish(const ClockTable& table, const PlaybackScore& playback);

    // Whether the last publish has been picked up.
    [[nodiscard]] bool publishAcknowledged() const noexcept;

    // The table of a generation still held in a slot: the published one and the
    // one before it. Null otherwise, or before the first publish.
    [[nodiscard]] const ClockTable* tableForGeneration(std::uint64_t generation) const noexcept;
    [[nodiscard]] std::uint64_t publishedGeneration() const noexcept;

    // False when the queue is full.
    [[nodiscard]] bool send(const ClockCommand& command) noexcept;

    void setMetronomeEnabled(bool enabled) noexcept;
    void setVolume(float gain) noexcept;
    void setAccentEnabled(bool enabled) noexcept;
    [[nodiscard]] bool metronomeEnabled() const noexcept;
    [[nodiscard]] float volume() const noexcept;
    [[nodiscard]] bool accentEnabled() const noexcept;

    // --- Playing the score (openspec/changes/archive/2026-11-08-score-playback) ----------------
    //
    // The synthesiser, once it has loaded: set once, and owned by the caller,
    // who keeps it alive until this renderer has left the audio callback.
    void setSynth(ScoreSynth* loaded) noexcept;
    [[nodiscard]] bool hasSynth() const noexcept;

    // Bit n set when track n sounds; zero, the default, plays nothing.
    void setAudibleTracks(std::uint64_t mask) noexcept;
    [[nodiscard]] std::uint64_t audibleTracks() const noexcept;
    void setPlaybackVolume(float gain) noexcept;

    // Note events skipped because a block held more than it may dispatch.
    [[nodiscard]] std::uint64_t playbackOverflows() const noexcept;

    // At most this many note events are dispatched in one block.
    static constexpr int maximumEventsPerBlock = 512;

    // Every note at the same strength until dynamics are read: 80 of 127.
    static constexpr float playbackVelocity = 80.0f / 127.0f;

    [[nodiscard]] ClockSnapshot snapshot() const noexcept;
    [[nodiscard]] ClockEventCounts events() const noexcept;

    // --- Recording a take (openspec/changes/archive/2026-11-08-takes, design.md decision 1) -----
    //
    // While armed, every block the clock plays is copied, with its gain, into
    // a preallocated FIFO, and a `RecordedBlock` saying where it starts is
    // pushed beside it. With no input -- muted, or none -- silence is recorded,
    // so the timeline stays whole. A block that does not fit is not pushed,
    // samples or stamp, and is counted as an overflow.

    // About five seconds at 48 kHz: the recorder drains it sixty times a
    // second.
    static constexpr std::size_t recordingCapacity = 262144;
    static constexpr std::size_t recordedBlockCapacity = 4096;

    // Message thread. Arming first discards anything left from before.
    void armRecording();
    void disarmRecording() noexcept;
    [[nodiscard]] bool recordingArmed() const noexcept;

    // Message thread: the stamps and the samples, in the order they were
    // pushed. Read a stamp, then exactly its `frames` samples.
    [[nodiscard]] bool popRecordedBlock(RecordedBlock& block) noexcept;
    [[nodiscard]] std::size_t readRecording(float* destination, std::size_t maximum) noexcept;
    [[nodiscard]] std::uint64_t recordingOverflows() const noexcept;

    // --- Audio thread -------------------------------------------------------

    void render(RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING override;

  private:
    struct Slot
    {
        ClockTable table;
        PlaybackScore playback;
        std::array<float, maximumClickFrames> accented{};
        std::array<float, maximumClickFrames> normal{};
        std::size_t clickFrames = 0;
        double clickRate = 0.0;
    };

    // A click sounding, carried across blocks when it straddles one.
    struct Voice
    {
        bool sounding = false;
        bool accented = false;
        std::size_t readPosition = 0;
    };

    static void synthesise(Slot& slot, double sampleRate);

    void pickUpTable() noexcept PRACTICE_TAKES_NONBLOCKING;
    void applyCommands() noexcept PRACTICE_TAKES_NONBLOCKING;
    void startCountIn(int bars) noexcept PRACTICE_TAKES_NONBLOCKING;
    void advance(RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING;
    void
    startClick(RenderBlock& block, int offset, bool accented) noexcept PRACTICE_TAKES_NONBLOCKING;
    void mixVoiceUntil(RenderBlock& block, int offset) noexcept PRACTICE_TAKES_NONBLOCKING;

    // The score's playback: get the synthesiser ready for the block, sound the
    // notes in one span of it, and render it up to an offset.
    void preparePlayback(const RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING;
    void dispatchNotes(RenderBlock& block, int offset, Frame start, Frame length) noexcept
        PRACTICE_TAKES_NONBLOCKING;
    void renderSynthUntil(RenderBlock& block, int offset) noexcept PRACTICE_TAKES_NONBLOCKING;
    // Every note released, and the event cursor found again: a seek, a wrap,
    // a stop, a new table.
    void releaseNotes() noexcept PRACTICE_TAKES_NONBLOCKING;
    // Copy one span of the block -- `length` frames from `offset`, which start
    // at clock frame `start` -- into the recording, if one is armed.
    void recordSpan(
        const RenderBlock& block,
        int offset,
        Frame start,
        Frame length,
        bool countIn) noexcept PRACTICE_TAKES_NONBLOCKING;

    void publishSnapshot(
        Frame blockStartPosition,
        std::uint64_t blockStartStream,
        const RenderBlock& block) noexcept PRACTICE_TAKES_NONBLOCKING;

    [[nodiscard]] const Slot* activeSlot() const noexcept PRACTICE_TAKES_NONBLOCKING;

    std::unique_ptr<std::array<Slot, 2>> slots;

    // The table hand-off.
    std::atomic<std::uint64_t> published{0};
    std::atomic<std::uint64_t> acknowledged{0};

    SpscQueue<ClockCommand, 64> commands;

    std::unique_ptr<AudioSampleFifo<recordingCapacity>> recording;
    std::unique_ptr<SpscQueue<RecordedBlock, recordedBlockCapacity>> recordedBlocks;
    std::atomic<bool> armed{false};
    std::atomic<std::uint64_t> overflowCount{0};

    // What is recorded while there is no input, so the timeline stays whole.
    static constexpr std::size_t silenceFrames = 1024;
    std::array<float, silenceFrames> silence{};

    std::atomic<ScoreSynth*> synthPointer{nullptr};
    std::atomic<std::uint64_t> audibleMask{0};
    std::atomic<float> playbackGain{0.8f};
    std::atomic<std::uint64_t> playbackOverflowCount{0};

    std::atomic<bool> metronomeOn{true};
    std::atomic<float> gain{0.7f};
    std::atomic<bool> accentOn{true};

    // Audio thread only.
    std::uint64_t current = 0;
    Frame position = 0;
    ClockState state = ClockState::stopped;
    bool countingIn = false;
    CountIn countIn;
    Voice voice;
    int mixedUpTo = 0;
    std::uint64_t streamFrames = 0;

    // Playback, audio thread only. `nextEvent` is meaningful only while
    // `eventsInSync`; `programsFor` is the table generation whose tracks'
    // programs the synthesiser has.
    ScoreSynth* synth = nullptr;
    std::size_t nextEvent = 0;
    bool eventsInSync = false;
    std::uint64_t programsFor = 0;
    std::uint64_t soundingTracks = 0;
    int synthRenderedUpTo = 0;
    int eventsThisBlock = 0;

    // The snapshot, under a sequence lock: odd while being written.
    std::atomic<std::uint32_t> snapshotSequence{0};
    std::atomic<Frame> snapshotPosition{0};
    std::atomic<std::uint8_t> snapshotState{0};
    std::atomic<bool> snapshotCountingIn{false};
    std::atomic<Frame> snapshotCountInTarget{0};
    std::atomic<std::uint64_t> snapshotGeneration{0};
    std::atomic<std::uint64_t> snapshotStreamFrame{0};
    std::atomic<Frame> snapshotBlockStart{0};
    std::atomic<double> snapshotBlockTime{0.0};
    std::atomic<double> snapshotSampleRate{0.0};

    std::atomic<std::uint64_t> seekCount{0};
    std::atomic<std::uint64_t> wrapCount{0};
    std::atomic<std::uint64_t> endCount{0};
};
} // namespace transport
