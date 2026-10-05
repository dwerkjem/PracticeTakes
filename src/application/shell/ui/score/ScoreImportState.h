#pragma once

#include <memory>
#include <utility>

#include "ScoreImportSummary.h"

#include "platform/score/Score.h"
#include "platform/score/musicxml/MusicXmlImportResult.h"

// Pure logic (no JUCE dependency) for what the shell holds between imports: the
// current score, and the summary of the import that produced it.
//
// Split out of `MainComponent` so that the two rules worth asserting can be
// tested without a display:
//
//  - a successful import replaces the current score;
//  - a **failed** import leaves it alone. Closing the score a user already has
//    open because a second file turned out to be malformed would be a worse
//    bug than the malformed file.
//
// The score is held as `std::shared_ptr<const Score>` and handed out by value,
// which is the ownership the score model's design asks for: `const` means any
// number of readers on any number of threads need no lock, and a copy means a
// reader's score stays alive and unchanged even after a different score is
// opened. The shell stands in for #39's session here.
class ScoreImportState
{
  public:
    // Apply a finished import, and return the summary to show for it.
    const ScoreImportSummary& apply(const score::musicxml::MusicXmlImportResult& result)
    {
        lastSummary = summariseScoreImport(result);
        hasSummary = true;

        // `MusicXmlImportResult` guarantees a non-null score exactly when the
        // status succeeded, but this checks the pointer rather than the status:
        // the one thing that must never happen here is replacing a usable
        // score with nothing.
        if (result.score != nullptr)
        {
            currentScore = result.score;
        }

        return lastSummary;
    }

    // The current score, or null before the first successful import. Returned
    // by value on purpose -- see the note on ownership above.
    [[nodiscard]] std::shared_ptr<const score::Score> score() const
    {
        return currentScore;
    }

    [[nodiscard]] bool hasScore() const noexcept
    {
        return currentScore != nullptr;
    }

    // The summary of the most recent import, successful or not. Only meaningful
    // once `hasImported()` is true.
    [[nodiscard]] const ScoreImportSummary& summary() const noexcept
    {
        return lastSummary;
    }

    [[nodiscard]] bool hasImported() const noexcept
    {
        return hasSummary;
    }

  private:
    std::shared_ptr<const score::Score> currentScore;
    ScoreImportSummary lastSummary;
    bool hasSummary = false;
};
