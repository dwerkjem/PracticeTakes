#include "BackgroundWork.h"

#include <utility>

namespace library
{
BackgroundWork::BackgroundWork() : juce::Thread("Practice Takes library") {}

BackgroundWork::~BackgroundWork()
{
    signalThreadShouldExit();
    jobArrived.signal();
    static_cast<void>(stopThread(-1));
    cancelPendingUpdate();
}

void BackgroundWork::post(Job job)
{
    {
        const juce::ScopedLock held(lock);
        pending.push_back(std::move(job));
    }
    if (!isThreadRunning())
    {
        static_cast<void>(startThread(juce::Thread::Priority::low));
    }
    jobArrived.signal();
}

void BackgroundWork::finishAll()
{
    for (;;)
    {
        {
            const juce::ScopedLock held(lock);
            if (pending.empty() && !working)
            {
                break;
            }
        }
        static_cast<void>(jobDone.wait(50));
    }
    // Taken here, not through handleUpdateNowIfNeeded: the worker records a
    // job as done before it triggers the update, so between the two there is
    // nothing pending to handle and the result would wait for the message
    // loop. handleAsyncUpdate takes what is finished under the lock; the
    // update that follows finds nothing, which is harmless.
    handleAsyncUpdate();
}

void BackgroundWork::run()
{
    while (!threadShouldExit())
    {
        Job job;
        {
            const juce::ScopedLock held(lock);
            if (!pending.empty())
            {
                job = std::move(pending.front());
                pending.pop_front();
                working = true;
            }
        }
        if (!job)
        {
            static_cast<void>(jobArrived.wait(1000));
            continue;
        }
        Finish finish = job();
        {
            const juce::ScopedLock held(lock);
            if (finish)
            {
                finished.push_back(std::move(finish));
            }
            working = false;
        }
        jobDone.signal();
        triggerAsyncUpdate();
    }
}

void BackgroundWork::handleAsyncUpdate()
{
    std::vector<Finish> done;
    {
        const juce::ScopedLock held(lock);
        done.swap(finished);
    }
    for (const Finish& finish : done)
    {
        finish();
    }
}
} // namespace library
