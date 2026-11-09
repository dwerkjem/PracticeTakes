#pragma once

#include "../../platform/evaluation/TakeReview.h"
#include "../../platform/take/TakeStatus.h"
#include "Localisation.h"
#include "NoteNames.h"

// The words for what the platform layer reports about takes, which keeps
// those as data so that it stays JUCE-free and composes no prose
// (openspec/changes/internationalization, decision 4).
namespace l10n
{
// What the transport says about a take: "Counting in", "Recording 0:12",
// "Take 3 stopped in bar 14, 0:42", "Take 3 failed: another score was opened".
[[nodiscard]] juce::String takeStatus(const take::Take& take);

// One row of the Take Review list: "Bar 3, beat 2 · G4 · wrong, played A4 · 40
// ms late", or "Bar 1, beat 2 · G#5 · extra".
[[nodiscard]] juce::String reviewRow(
    const take::Take& take,
    const evaluation::NoteReport& report,
    const evaluation::ReviewRow& row);

// "21 correct · 2 wrong · 1 missed · 3 extra · 4 early · 2 late", leaving out
// what is zero. Early and late count moments, not the notes of a chord. A sung
// take's correct notes are "in tune", "sharp" and "flat" instead, and its
// unsure ones "uncertain".
[[nodiscard]] juce::String reviewSummary(const evaluation::NoteReport& report);
} // namespace l10n
