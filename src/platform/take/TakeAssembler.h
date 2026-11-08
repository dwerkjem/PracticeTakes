#pragma once

#include <cstddef>

#include "Take.h"

// Appends what the renderer recorded to a take's audio, block by block (no JUCE
// dependency). openspec/changes/takes/design.md, decision 6.
//
// Each block arrives with the clock frame it started at. A block continues the
// take when it starts exactly where the audio so far ends; anything else is a
// discontinuity -- a seek, a loop's wrap, a dropped block -- which a locked
// clock never makes. It is reported rather than papered over, because every
// timing judgement after this reads the audio as one unbroken run.
namespace take
{
enum class AssemblyResult
{
    appended,
    discontinuity
};

[[nodiscard]] inline AssemblyResult
appendBlock(Take& target, const transport::RecordedBlock& block, const float* samples)
{
    if (block.frames == 0)
    {
        return AssemblyResult::appended;
    }

    if (target.audio.empty())
    {
        target.audioStart = block.position;
    }
    else if (block.position != target.audioEnd())
    {
        return AssemblyResult::discontinuity;
    }

    target.audio.insert(target.audio.end(), samples, samples + block.frames);
    if (block.countingIn)
    {
        target.countInFrames += static_cast<Frame>(block.frames);
    }
    return AssemblyResult::appended;
}
} // namespace take
