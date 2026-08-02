#pragma once

#include <memory>
#include <utility>

#include "Score.h"

// Assembles a score, then freezes it.
//
// The model is value types plus this builder rather than a mutable object graph
// for one reason: it makes "immutable after import" enforceable by the type
// system instead of by convention. A Score is fully built before it is first
// shared, and what is shared is std::shared_ptr<const Score> -- so any number
// of readers on any number of threads need no lock, and no consumer can mutate
// a score another consumer is mid-way through reading.
//
// Import runs on a background thread (reading, unzipping, and parsing a score
// is unbounded work with file I/O), and the finished value crosses to the
// message thread. The audio thread never sees a Score at all; see the ownership
// notes on Score itself for why, and what playback must do instead.
namespace score
{
class ScoreBuilder
{
  public:
    ScoreBuilder() = default;

    // Mutable access to the score under construction. Only valid before
    // `build`; the whole point of the class is that this access ends.
    [[nodiscard]] Score& draft() noexcept
    {
        return draft_;
    }

    [[nodiscard]] const Score& draft() const noexcept
    {
        return draft_;
    }

    // Record something about the file being read. Available during
    // construction so the importer can report as it goes rather than
    // accumulating diagnostics separately and merging them at the end.
    void addDiagnostic(Diagnostic diagnostic)
    {
        draft_.diagnostics.push_back(std::move(diagnostic));
    }

    // Apply every invariant, repairing violations and appending a diagnostic
    // for each, then hand back the finished score as an immutable shared value.
    //
    // The builder is left empty afterwards, so a second `build` cannot hand out
    // a second mutable handle on the same data.
    [[nodiscard]] std::shared_ptr<const Score> build();

  private:
    Score draft_;
};

// Apply the invariants to a score that was assembled without the builder --
// by a test, say -- and freeze it the same way.
[[nodiscard]] std::shared_ptr<const Score> freeze(Score score);
} // namespace score
