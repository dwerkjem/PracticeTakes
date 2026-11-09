#include "Intonation.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "../pitch/ReferencePitch.h"

namespace evaluation
{
namespace
{
[[nodiscard]] double median(std::vector<double> values)
{
    if (values.empty())
    {
        return 0.0;
    }
    const std::size_t middle = values.size() / 2;
    std::nth_element(
        values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if (values.size() % 2 == 1)
    {
        return upper;
    }
    const double lower =
        *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return 0.5 * (lower + upper);
}

[[nodiscard]] std::int64_t framesOf(double milliseconds, double sampleRate)
{
    return static_cast<std::int64_t>(std::llround(milliseconds * sampleRate / 1000.0));
}
} // namespace

double centsFrom(double hertz, int midi, double referenceHz) noexcept
{
    return 100.0 * (pitch::frequencyToMidi(hertz, referenceHz) - static_cast<double>(midi));
}

std::pair<double, int> foldOctaves(double cents) noexcept
{
    const auto octaves = static_cast<int>(std::floor((cents + 600.0) / 1200.0));
    return {cents - (1200.0 * octaves), octaves};
}

Intonation judgeIntonation(
    const PitchContour& contour,
    std::int64_t from,
    std::int64_t to,
    int writtenMidi,
    double referenceHz,
    double sampleRate,
    const IntonationSettings& settings)
{
    Intonation result;
    if (to <= from || sampleRate <= 0.0)
    {
        return result;
    }

    // The regions (decision 2). A note too short for a sustain between its
    // attack and release is judged on its middle half.
    const std::int64_t length = to - from;
    const auto lengthMs = static_cast<double>(length) * 1000.0 / sampleRate;
    const double attackMs = std::clamp(
        lengthMs * settings.attackFraction, settings.minimumAttackMs, settings.maximumAttackMs);
    const double releaseMs =
        std::min(lengthMs * settings.releaseFraction, settings.maximumReleaseMs);
    std::int64_t sustainFrom = from + framesOf(attackMs, sampleRate);
    std::int64_t sustainTo = to - framesOf(releaseMs, sampleRate);
    if (sustainTo - sustainFrom < length / 2)
    {
        sustainFrom = from + (length / 4);
        sustainTo = to - (length / 4);
    }

    std::vector<double> sure;
    std::map<int, std::size_t> octaves;
    const auto [first, last] = contour.between(from, to);
    result.frames.reserve(last - first);
    for (std::size_t index = first; index < last; ++index)
    {
        const ContourFrame& frame = contour.frames[index];
        IntonationFrame judged;
        judged.frame = frame.frame;
        judged.region = frame.frame < sustainFrom  ? NoteRegion::attack
                        : frame.frame >= sustainTo ? NoteRegion::release
                                                   : NoteRegion::sustain;
        judged.voiced = frame.voiced();
        if (judged.voiced)
        {
            const auto [folded, octave] =
                foldOctaves(centsFrom(frame.hertz, writtenMidi, referenceHz));
            judged.cents = folded;
            judged.sure = frame.confidence >= settings.sureConfidence;
            if (judged.sure && judged.region == NoteRegion::sustain)
            {
                sure.push_back(folded);
                ++octaves[octave];
            }
        }
        if (judged.region == NoteRegion::sustain)
        {
            ++result.sustainFrames;
        }
        result.frames.push_back(judged);
    }

    // Uncertain, not wrong, without enough sure frames (decision 4).
    result.sureFrames = sure.size();
    const double share =
        result.sustainFrames > 0
            ? static_cast<double>(sure.size()) / static_cast<double>(result.sustainFrames)
            : 0.0;
    if (sure.size() < settings.minimumSureFrames || share < settings.minimumSureShare)
    {
        return result;
    }

    // The median, its spread, and the share in tune (decision 3).
    result.cents = median(sure);
    std::vector<double> distances;
    distances.reserve(sure.size());
    std::size_t inTolerance = 0;
    for (const double cents : sure)
    {
        distances.push_back(std::abs(cents - result.cents));
        if (std::abs(cents) <= settings.toleranceCents)
        {
            ++inTolerance;
        }
    }
    result.spreadCents = median(distances);
    result.score = static_cast<double>(inTolerance) / static_cast<double>(sure.size());

    // The commonest octave; ties to the written one, then the lower.
    std::size_t most = 0;
    for (const auto& [octave, count] : octaves)
    {
        if (count > most || (count == most && octave == 0))
        {
            most = count;
            result.octave = octave;
        }
    }

    result.zone = pitch::zoneOf(result.cents, settings.toleranceCents);
    const double size = std::abs(result.cents);
    if (size > settings.wrongCents)
    {
        result.verdict = IntonationVerdict::wrong;
        result.sungMidi = writtenMidi + (12 * result.octave) +
                          static_cast<int>(std::lround(result.cents / 100.0));
    }
    else if (size <= settings.toleranceCents)
    {
        result.verdict = IntonationVerdict::inTune;
    }
    else
    {
        result.verdict = result.cents > 0.0 ? IntonationVerdict::sharp : IntonationVerdict::flat;
    }
    return result;
}

std::optional<int> pitchAfterOnset(
    const PitchContour& contour,
    std::int64_t frame,
    double referenceHz,
    double sampleRate,
    const IntonationSettings& settings)
{
    const auto [first, last] = contour.between(
        frame + framesOf(settings.extraFromMs, sampleRate),
        frame + framesOf(settings.extraToMs, sampleRate));
    std::vector<double> sure;
    for (std::size_t index = first; index < last; ++index)
    {
        const ContourFrame& candidate = contour.frames[index];
        if (candidate.voiced() && candidate.confidence >= settings.sureConfidence)
        {
            sure.push_back(pitch::frequencyToMidi(candidate.hertz, referenceHz));
        }
    }
    if (sure.size() < settings.minimumSureFrames)
    {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(median(sure)));
}
} // namespace evaluation
