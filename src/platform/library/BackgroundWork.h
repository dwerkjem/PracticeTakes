#pragma once

#include <juce_events/juce_events.h>

#include <deque>
#include <functional>
#include <vector>

// Work the library does off the message thread -- listing the folder, loading a
// take -- one job at a time, each handing back what to do with its result on
// the message thread (openspec/changes/media-library, decisions 5 and 6).
//
// A job is a function run on the worker that returns a function run on the
// message thread. Whatever the second one touches, it must check is still
// there: it runs later, from the message loop.
namespace library
{
class BackgroundWork final : private juce::Thread, private juce::AsyncUpdater
{
  public:
    using Finish = std::function<void()>;
    using Job = std::function<Finish()>;

    BackgroundWork();
    ~BackgroundWork() override;

    BackgroundWork(const BackgroundWork&) = delete;
    BackgroundWork& operator=(const BackgroundWork&) = delete;

    void post(Job job);

    // Block until every posted job has run, then run what each handed back.
    // For tests and the capture harness.
    void finishAll();

  private:
    void run() override;
    void handleAsyncUpdate() override;

    juce::CriticalSection lock;
    std::deque<Job> pending;
    std::vector<Finish> finished;
    bool working = false;
    juce::WaitableEvent jobArrived;
    juce::WaitableEvent jobDone;
};
} // namespace library
