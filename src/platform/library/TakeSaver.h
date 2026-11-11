#pragma once

#include <juce_events/juce_events.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../take/Take.h"
#include "LibraryLayout.h"

// Saves each take the recorder keeps into the library's `Takes/` folder, on a
// thread of its own (openspec/changes/media-library, decision 3).
//
// A kept take is a `shared_ptr<const Take>` that nothing changes again, so the
// thread reads it without a lock. Only which takes to save, and what became of
// each, cross between threads, and both are behind one lock.
namespace library
{
// Of `kept`, oldest first, the takes to save: newer than `lastSeenId`, and not
// loaded from the library already.
[[nodiscard]] std::vector<std::shared_ptr<const take::Take>>
takesToSave(const std::vector<std::shared_ptr<const take::Take>>& kept, std::uint64_t lastSeenId);

class TakeSaver final : private juce::Thread, private juce::AsyncUpdater
{
  public:
    struct Saved
    {
        std::uint64_t id = 0;
        juce::File folder;

        // Empty when it was saved; otherwise why not, as a catalogue
        // identifier.
        std::string error;
    };

    TakeSaver();
    ~TakeSaver() override;

    TakeSaver(const TakeSaver&) = delete;
    TakeSaver& operator=(const TakeSaver&) = delete;

    // Save, into `layout`, the takes of `kept` it has not seen yet. Called on
    // the message thread whenever the recorder's takes change; a take seen
    // once is never offered again.
    void offer(const Layout& layout, const std::vector<std::shared_ptr<const take::Take>>& kept);

    // Told, on the message thread, of every take saved or not saved.
    std::function<void(const Saved&)> onSaved;

    // Block until every offered take is written, then report them. For tests
    // and the capture harness.
    void finishSaving();

  private:
    struct Job
    {
        std::shared_ptr<const take::Take> recorded;
        Layout layout;
        std::int64_t recordedMs = 0;
    };

    void run() override;
    void handleAsyncUpdate() override;

    std::uint64_t lastSeenId = 0;

    juce::CriticalSection lock;
    std::deque<Job> pending;
    std::vector<Saved> finished;
    bool writing = false;
    juce::WaitableEvent jobArrived;
    juce::WaitableEvent jobDone;
};
} // namespace library
