#include "TakeSaver.h"

#include <utility>

#include "TakeFiles.h"

namespace library
{
std::vector<std::shared_ptr<const take::Take>>
takesToSave(const std::vector<std::shared_ptr<const take::Take>>& kept, std::uint64_t lastSeenId)
{
    std::vector<std::shared_ptr<const take::Take>> result;
    for (const auto& recorded : kept)
    {
        if (recorded != nullptr && recorded->id > lastSeenId && recorded->libraryFolder.empty() &&
            take::isKept(recorded->state()))
        {
            result.push_back(recorded);
        }
    }
    return result;
}

TakeSaver::TakeSaver() : juce::Thread("Practice Takes take saver") {}

TakeSaver::~TakeSaver()
{
    // A take being written is finished, not cut off: it is a recording that
    // cannot be made again. Only what has not started yet is left.
    signalThreadShouldExit();
    jobArrived.signal();
    static_cast<void>(stopThread(-1));
    cancelPendingUpdate();
}

void TakeSaver::offer(
    const Layout& layout,
    const std::vector<std::shared_ptr<const take::Take>>& kept)
{
    const auto fresh = takesToSave(kept, lastSeenId);
    if (fresh.empty())
    {
        return;
    }
    const std::int64_t now = juce::Time::currentTimeMillis();
    {
        const juce::ScopedLock held(lock);
        for (const auto& recorded : fresh)
        {
            pending.push_back(Job{recorded, layout, now});
            lastSeenId = std::max(lastSeenId, recorded->id);
        }
    }
    if (!isThreadRunning())
    {
        static_cast<void>(startThread(juce::Thread::Priority::low));
    }
    jobArrived.signal();
}

void TakeSaver::finishSaving()
{
    for (;;)
    {
        {
            const juce::ScopedLock held(lock);
            if (pending.empty() && !writing)
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

void TakeSaver::run()
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
                writing = true;
            }
        }
        if (job.recorded == nullptr)
        {
            static_cast<void>(jobArrived.wait(1000));
            continue;
        }

        Saved saved;
        saved.id = job.recorded->id;
        const juce::File takes = job.layout.takes();
        if (job.layout.root == juce::File() || !takes.createDirectory())
        {
            saved.error = l10n::key("library.take.error.unwritable");
        }
        else
        {
            saved.folder = newTakeFolder(
                takes, juce::Time(job.recordedMs), juce::String(job.recorded->scoreTitle));
            saved.error = writeTake(
                saved.folder, *job.recorded, juce::File(juce::String(job.recorded->scoreFile)),
                job.recordedMs);
            if (!saved.error.empty())
            {
                saved.folder = juce::File();
            }
        }

        {
            const juce::ScopedLock held(lock);
            finished.push_back(std::move(saved));
            writing = false;
        }
        jobDone.signal();
        triggerAsyncUpdate();
    }
}

void TakeSaver::handleAsyncUpdate()
{
    std::vector<Saved> done;
    {
        const juce::ScopedLock held(lock);
        done.swap(finished);
    }
    if (onSaved)
    {
        for (const Saved& saved : done)
        {
            onSaved(saved);
        }
    }
}
} // namespace library
