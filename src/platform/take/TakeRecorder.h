#pragma once

#include <juce_events/juce_events.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../evaluation/NoteMatching.h"
#include "../midi/MidiEvent.h"
#include "../score/CurrentScore.h"
#include "../transport/PracticeClock.h"
#include "Take.h"

// Records takes: the message-thread half of openspec/changes/archive/2026-11-08-takes.
//
// It starts a take by locking the practice clock, arming its renderer to record
// the microphone, and playing with the count-in; follows the clock to move the
// take through its lifecycle; drains the recorded audio and the keyboard's
// events onto the take's timeline; and ends the take -- completed at the end of
// the piece, stopped early, cancelled, or failed with a reason -- keeping every
// take but a cancelled one for the session. A kept take is reviewed: judged
// against the part of its score it matches best, on a worker thread, and held
// for the Score View and the Take Review tool
// (openspec/changes/archive/2026-11-08-take-review).
//
// Its sources arrive as functions rather than as the services themselves, so it
// is tested headless with blocks and key events supplied by the test.
// MainComponent wires them to AudioInputService and MidiInputService.
namespace take
{
// Where a take's input comes from. Any left empty reads as absent.
struct TakeSources
{
    // An input device is open (or the synthetic tone stands in for one).
    std::function<bool()> microphoneOpen;
    std::function<bool()> microphoneMuted;

    // A MIDI keyboard is open.
    std::function<bool()> keyboardOpen;

    // Start and stop listening to the keyboard, around a take.
    std::function<void()> beginKeyboard;
    std::function<void()> endKeyboard;

    // Every keyboard event since the last call, up to `maximum`.
    std::function<std::size_t(midi::MidiEvent* destination, std::size_t maximum)> readKeyboard;

    // The device's stated input and output latencies, in frames.
    std::function<std::pair<int, int>()> latencies;

    // The application's reference pitch, in hertz: what a take is sung
    // against. 440 when absent.
    std::function<double()> referencePitch;
};

class TakeListener
{
  public:
    virtual ~TakeListener() = default;

    // A take started, changed state, or ended, or the review changed.
    virtual void takesChanged() = 0;
};

// A kept take, judged against one part of its score
// (openspec/changes/archive/2026-11-08-take-review/design.md, decision 1).
struct TakeReview
{
    std::shared_ptr<const Take> take;
    std::size_t part = 0;
    evaluation::NoteReport report;
};

class TakeRecorder final : private juce::Timer, private transport::ClockListener
{
  public:
    // Takes kept for the session, oldest dropped first (design.md decision 5).
    static constexpr std::size_t maximumTakes = 50;

    static constexpr int drainRateHz = 30;

    TakeRecorder(
        transport::PracticeClock& practiceClock,
        const score::CurrentScore& currentScore,
        TakeSources sources);
    ~TakeRecorder() override;

    TakeRecorder(const TakeRecorder&) = delete;
    TakeRecorder& operator=(const TakeRecorder&) = delete;

    // Why a take cannot start now, in a phrase for the interface; nothing when
    // it can.
    [[nodiscard]] std::optional<std::string> whyCannotStart() const;

    // Start a take from where the clock is, counting in. False, changing
    // nothing, when one cannot start.
    bool start();

    // End the live take: stopped early (or cancelled, if nothing was played
    // yet), and stop the clock.
    void stop();

    // Discard the live take.
    void cancel();

    [[nodiscard]] bool isRecording() const noexcept;

    // The live take, or null.
    [[nodiscard]] const Take* current() const noexcept;

    // Every kept take of the session, oldest first.
    [[nodiscard]] const std::vector<std::shared_ptr<const Take>>& takes() const noexcept;

    // The take that ended most recently, cancelled ones included: what the
    // transport says about "the last take". Null before the first.
    [[nodiscard]] const Take* lastEnded() const noexcept;

    // Keep an ended take recorded elsewhere -- the capture harness's -- as if
    // it had just ended here: it gets an id, joins the kept takes, and is
    // reviewed. Returns the id; 0, keeping nothing, for a take that has not
    // ended or was cancelled.
    std::uint64_t keep(Take take);

    // Review a kept take, against `part` or, with none, the part it matches
    // best (design.md decision 2). False, changing nothing, for a take that is
    // not kept or a part its score does not have.
    bool reviewTake(std::uint64_t id, std::optional<std::size_t> part = std::nullopt);

    // How sung notes are judged: the singer's tolerance. A change judges the
    // take under review again (openspec/changes/archive/2026-11-09-sung-pitch-judging, decision 7).
    void setIntonation(const evaluation::IntonationSettings& settings);
    [[nodiscard]] const evaluation::IntonationSettings& intonationSettings() const noexcept;

    // The take under review, judged. Null before the first, and while a
    // different take is being judged.
    [[nodiscard]] const TakeReview* review() const noexcept;

    // A review is being judged, on a worker thread: a microphone take's onsets
    // take about a second for three minutes.
    [[nodiscard]] bool isJudging() const noexcept;

    // Forget every kept take and the review, leaving a live take alone. The
    // capture harness starts each state from none, so no picture shows the
    // last one's takes.
    void forgetTakes();

    // Wait for the judging in progress and take its result. For tests and the
    // capture harness; the timer takes it otherwise.
    void finishJudging();

    void addListener(TakeListener* listener);
    void removeListener(TakeListener* listener);

    // Drain and check. The timer calls it; a test calls it between blocks.
    void poll();

  private:
    void timerCallback() override;
    void clockChanged(const transport::ClockUpdate& update) override;

    void follow(const transport::ClockUpdate& update);
    // Move what was recorded onto the live take. False when the audio did not
    // continue where it left off; the caller ends the take.
    [[nodiscard]] bool drain();
    void finish(TakeEvent event, std::string reason = {});
    void notify();

    // Keep a take that has ended, oldest dropped past the maximum.
    void retain(std::shared_ptr<const Take> take);

    // Take a finished judgement, if the newest one is done.
    void adoptJudgement(bool wait);

    [[nodiscard]] bool microphoneUsable() const;
    [[nodiscard]] bool keyboardUsable() const;

    transport::PracticeClock& clock;
    const score::CurrentScore& scoreHolder;
    TakeSources sources;

    std::unique_ptr<Take> live;

    // Whether the clock has been seen running since the take started: before
    // that, its state is what it was before Record was pressed.
    bool sawRunning = false;

    // The bar the clock was last seen in while running, for a stopped take.
    std::int64_t lastBar = 0;

    std::vector<std::shared_ptr<const Take>> kept;
    std::shared_ptr<const Take> ended;
    std::uint64_t nextId = 0;

    // The judgement in progress, and the review it replaces. Only the newest
    // request's result is taken; an older one finishes and is dropped.
    struct Judging
    {
        std::uint64_t request = 0;
        std::future<TakeReview> result;
    };
    std::vector<Judging> judging;
    std::uint64_t nextRequest = 0;
    std::optional<TakeReview> reviewed;

    // The newest review asked for, to judge again when the settings change.
    struct Requested
    {
        std::uint64_t id = 0;
        std::optional<std::size_t> part;
    };
    std::optional<Requested> requested;
    evaluation::IntonationSettings intonation;

    std::vector<float> scratch;
    std::vector<midi::MidiEvent> keyBuffer;
    std::vector<TakeListener*> listeners;
};
} // namespace take
