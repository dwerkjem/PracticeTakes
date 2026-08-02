#include "ScoreBuilder.h"

#include "ScoreInvariants.h"

namespace score
{
std::shared_ptr<const Score> ScoreBuilder::build()
{
    // Move out first, so the builder cannot hand out a second mutable handle on
    // the same data if it is built twice.
    Score finished = std::move(draft_);
    draft_ = Score{};

    return freeze(std::move(finished));
}

std::shared_ptr<const Score> freeze(Score score)
{
    enforceInvariants(score);

    // shared_ptr<const Score> is the enforcement mechanism: the score is
    // complete at this point and nothing can mutate it afterwards, so readers
    // need no lock and a score stays alive as long as any reader holds it --
    // even if the session swaps in a different one.
    return std::make_shared<const Score>(std::move(score));
}
} // namespace score
