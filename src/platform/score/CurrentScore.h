#pragma once

#include <memory>
#include <utility>

#include "Score.h"

// The score the application currently has open (no JUCE dependency).
//
// This is the seam a tool reads a score through. Before it existed, the
// imported score lived as a `shared_ptr` member of the shell and nothing
// outside the shell could reach it; `ToolServices` now carries a reference to
// one of these, so a tool reads the current score the same way it reads shared
// audio -- through the service bundle it was constructed with, never by
// reaching back into the shell.
//
// Ownership is the model's own (Score.h): a score is `shared_ptr<const Score>`,
// fully built before its first share and never mutated after, so `const` is
// what makes any number of readers safe without a lock. Both `publish` and
// `score` hand the pointer over by value, which is the point -- a reader's score
// stays alive and unchanged even after a different one is opened.
//
// **Message thread only.** A `shared_ptr` member is not safe to read and write
// concurrently, and the constraint rather than a mutex is deliberate: import
// already hands its finished score back to the message thread before anything
// publishes it, and the audio thread may not touch a Score at all (Score.h says
// why: a refcount copy is a contended atomic write and the tree is heap
// pointer-chasing). A lock here would buy safety against a use the design
// forbids, while making that use look supported.
namespace score
{
class CurrentScore
{
  public:
    // Replace the current score. Publishing a null pointer clears it, which no
    // caller does today -- a failed import is required to leave the open score
    // alone, and that rule lives in the caller (ScoreImportState) rather than
    // here, because "what a failed import does" is not this type's business.
    void publish(std::shared_ptr<const Score> newScore)
    {
        current = std::move(newScore);
    }

    // The current score, or null before the first publish.
    //
    // By value, and that is what lets a reader detect a change by comparing the
    // pointer it last rendered against this one -- no generation counter
    // needed. It is sound rather than merely cheap: while a reader holds its
    // copy the old Score cannot be freed, so its address cannot be reused by
    // the next one, and two distinct scores are always two distinct pointers.
    [[nodiscard]] std::shared_ptr<const Score> score() const
    {
        return current;
    }

    [[nodiscard]] bool hasScore() const noexcept
    {
        return current != nullptr;
    }

  private:
    std::shared_ptr<const Score> current;
};
} // namespace score
