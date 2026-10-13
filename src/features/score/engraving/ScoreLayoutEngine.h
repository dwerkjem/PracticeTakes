#pragma once

#include <memory>

#include "ScoreLayout.h"
#include "platform/score/Score.h"

// Turning a score into glyphs with positions (no JUCE dependency).
namespace engraving
{
// Lay out the first staff of the first part of `score` to fit
// `options.availableWidth`.
//
// Deterministic: the same score at the same width gives identical numbers,
// every time, with no dependence on anything outside its arguments. That is
// what makes the result snapshot testable without a display, and it is checked
// by a test rather than merely intended.
//
// A null score is not an error -- it is the ordinary state of an application
// that has not opened a file, and it comes back as `LayoutOutcome::noScore`.
//
// What this does not draw is in `ScoreLayout::notices` rather than left out
// silently. See proposal.md § Out of scope for the list and the reasons.
[[nodiscard]] ScoreLayout
layOutScore(const std::shared_ptr<const score::Score>& score, const LayoutOptions& options);
} // namespace engraving
