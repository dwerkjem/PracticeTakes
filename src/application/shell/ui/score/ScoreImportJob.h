#pragma once

#include <JuceHeader.h>

#include <functional>

#include "platform/score/musicxml/MusicXmlImportResult.h"

// The background job that reads a MusicXML file.
//
// Reading, decompressing, and DOM-parsing a score is unbounded work with file
// I/O: it cannot run on the message thread without freezing the window, and it
// must never be anywhere near the audio thread. So it runs here, and the
// finished result crosses back to the message thread as the
// `std::shared_ptr<const Score>` the score model's design is built around.
//
// This deliberately follows `FeedbackComponent`'s shape -- a `juce::Thread`
// subclass whose `run()` does the work and whose result is delivered with
// `juce::MessageManager::callAsync`, capturing a `juce::Component::SafePointer`
// -- rather than introducing a second threading idiom. The shell now has two
// consistent examples of a background job instead of two different ones.
//
// Ownership invariant: this is a member of the component passed as `owner`, so
// the job cannot outlive it, and `~ScoreImportJob` stops the thread before that
// component's own destructor runs. The `SafePointer` is the second line of
// defence, for a result already queued when the component goes away.
class ScoreImportJob final : private juce::Thread
{
  public:
    // The result is handed over by const reference: the shell reads it and
    // copies the shared_ptr it wants, so nothing needs to take ownership of
    // the whole result.
    using Callback = std::function<void(const score::musicxml::MusicXmlImportResult&)>;

    ScoreImportJob(juce::Component& ownerComponent, Callback finishedCallback);
    ~ScoreImportJob() override;

    ScoreImportJob(const ScoreImportJob&) = delete;
    ScoreImportJob& operator=(const ScoreImportJob&) = delete;

    // Begin reading `file`. Returns false when an import is already running, in
    // which case nothing is started and nothing is cancelled -- see
    // `fileBeingRead()` for what to tell the user.
    bool start(const juce::File& file);

    [[nodiscard]] bool isRunning() const;

    // The file the running import is reading, or a default `juce::File` when
    // none is running. The caller needs this to say *which* file is being read:
    // refusing a second import without saying why looks like the command was
    // ignored.
    [[nodiscard]] juce::File fileBeingRead() const;

  private:
    void run() override;

    juce::Component::SafePointer<juce::Component> owner;
    Callback onFinished;

    // Written on the message thread before the thread starts, read on both.
    // Guarded because `fileBeingRead()` is a message-thread query about a value
    // the import thread is using.
    juce::CriticalSection pendingFileLock;
    juce::File pendingFile;
};
