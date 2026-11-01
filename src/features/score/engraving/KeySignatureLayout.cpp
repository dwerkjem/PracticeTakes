#include "KeySignatureLayout.h"

#include "StaffPosition.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>

namespace engraving
{
namespace
{
constexpr int maximumAccidentals = 7;

// The circle of fifths, in the order the accidentals are drawn.
constexpr std::array<score::Step, maximumAccidentals> sharpOrder{
    score::Step::f, score::Step::c, score::Step::g, score::Step::d,
    score::Step::a, score::Step::e, score::Step::b};

constexpr std::array<score::Step, maximumAccidentals> flatOrder{
    score::Step::b, score::Step::e, score::Step::a, score::Step::d,
    score::Step::g, score::Step::c, score::Step::f};

// Half-space positions for a **G clef on line 2**. Treble F-sharp is the top
// line; treble B-flat is the middle line. Hand-checked against the engraved
// convention, which is the only way these can be got right -- see the header.
constexpr std::array<StaffSteps, maximumAccidentals> trebleSharpPositions{0, 3, -1, 2, 5, 1, 4};
constexpr std::array<StaffSteps, maximumAccidentals> trebleFlatPositions{4, 1, 5, 2, 6, 3, 7};

// What to add to the treble positions for the other two pitched signs, on their
// own reference line. One half space for the alto C clef, two for the bass F
// clef: that is the whole of the difference, for every accidental in both
// directions.
struct SignPlacement
{
    int referenceLine = 2;
    StaffSteps offset = 0;
};

[[nodiscard]] std::optional<SignPlacement> placementFor(const score::Clef& clef)
{
    if (clef.sign == "G")
    {
        return SignPlacement{2, 0};
    }

    if (clef.sign == "C")
    {
        return SignPlacement{3, 1};
    }

    if (clef.sign == "F")
    {
        return SignPlacement{4, 2};
    }

    return std::nullopt;
}
} // namespace

int keySignatureAccidentalCount(const score::KeySignature& key)
{
    return std::min(std::abs(key.fifths), maximumAccidentals);
}

bool keySignatureIsConventional(const score::Clef& clef)
{
    const std::optional<SignPlacement> placement = placementFor(clef);

    return placement.has_value() && placement->referenceLine == clef.line;
}

std::vector<KeySignatureAccidental>
keySignatureAccidentals(const score::Clef& clef, const score::KeySignature& key)
{
    const std::optional<SignPlacement> placement = placementFor(clef);

    if (!placement.has_value())
    {
        return {};
    }

    const int count = keySignatureAccidentalCount(key);
    const bool sharps = key.fifths > 0;

    // A clef on a line other than its sign's reference moves every pitch on the
    // staff by twice the line difference, so the whole signature moves with it
    // and the pitches stay the ones the key names.
    const StaffSteps lineShift = (placement->referenceLine - clef.line) * 2;

    std::vector<KeySignatureAccidental> accidentals;
    accidentals.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        const auto slot = static_cast<std::size_t>(index);

        accidentals.push_back(KeySignatureAccidental{
            sharps ? sharpOrder.at(slot) : flatOrder.at(slot),
            (sharps ? trebleSharpPositions.at(slot) : trebleFlatPositions.at(slot)) +
                placement->offset + lineShift,
            sharps ? AccidentalKind::sharp : AccidentalKind::flat});
    }

    return accidentals;
}
} // namespace engraving
