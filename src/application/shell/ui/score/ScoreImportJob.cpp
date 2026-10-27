#include "ScoreImportJob.h"

#include "platform/score/musicxml/MusicXmlImporter.h"

ScoreImportJob::ScoreImportJob(juce::Component& ownerComponent, Callback finishedCallback)
    : Thread("Score import"), owner(&ownerComponent), onFinished(std::move(finishedCallback))
{
}

ScoreImportJob::~ScoreImportJob()
{
    signalThreadShouldExit();

    // Waits rather than imposing a deadline, which is the opposite of
    // `FeedbackComponent`'s bounded HTTP timeout and deliberate:
    // `importMusicXmlFile` is one synchronous call that does not poll
    // `threadShouldExit()`, so a deadline here would mean JUCE forcibly killing
    // a thread in the middle of a DOM parse. A slow quit is a visible
    // annoyance; a killed parser is a corrupted heap.
    //
    // "Forever" is bounded in practice by the importer's own limits --
    // `MusicXmlImportLimits::maximumSourceBytes` is 64 MB, and it refuses
    // anything larger before reading it -- so the work this waits on always
    // terminates.
    stopThread(-1);
}

bool ScoreImportJob::start(const juce::File& file)
{
    // One import at a time. The running one is kept rather than cancelled: the
    // user asked for the first file, and silently abandoning it to start the
    // second is not an obvious improvement on refusing the second.
    if (isThreadRunning())
    {
        return false;
    }

    {
        const juce::ScopedLock lock(pendingFileLock);
        pendingFile = file;
    }

    startThread();

    return true;
}

bool ScoreImportJob::isRunning() const
{
    return isThreadRunning();
}

juce::File ScoreImportJob::fileBeingRead() const
{
    if (!isThreadRunning())
    {
        return {};
    }

    const juce::ScopedLock lock(pendingFileLock);

    return pendingFile;
}

void ScoreImportJob::run()
{
    juce::File file;

    {
        const juce::ScopedLock lock(pendingFileLock);
        file = pendingFile;
    }

    score::musicxml::MusicXmlImportResult result = score::musicxml::importMusicXmlFile(file);

    // Abandon the result rather than delivering it when the application is on
    // its way down: the destructor has already signalled, and the owner is
    // about to go.
    if (threadShouldExit())
    {
        return;
    }

    juce::MessageManager::callAsync(
        [safe = owner, callback = onFinished, outcome = std::move(result)]
        {
            // The owner component was destroyed between the import finishing
            // and this reaching the message thread. Dropping the result is the
            // whole point of the SafePointer: the alternative is calling into
            // a destroyed component, which is the crash-on-quit this guards.
            if (safe == nullptr)
            {
                return;
            }

            if (callback)
            {
                callback(outcome);
            }
        });
}
