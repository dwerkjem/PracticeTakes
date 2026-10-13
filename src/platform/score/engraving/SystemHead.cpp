#include "SystemHead.h"

#include <algorithm>
#include <array>
#include <string>

namespace score::engraving
{
namespace
{
// Blank space before the clef, and between one head element and the next.
constexpr double headIndent = 1.0;
constexpr double headElementGap = 0.8;

// Key signature accidentals are set tightly -- they read as one object, not as
// a row of separate accidentals -- so this is much smaller than the gap
// between head elements.
constexpr double keyAccidentalGap = 0.1;

// Room left between the head and the first column of music.
constexpr double headTrailingGap = 1.0;

// Vertical centres of the two time signature digits: the numerator spans the
// upper half of the staff and the denominator the lower, which puts their
// origins on the second line from each edge. SMuFL draws a time signature
// digit centred on its origin, so these are centres and not baselines.
constexpr int timeSignatureNumeratorStep = 6;
constexpr int timeSignatureDenominatorStep = 2;

// The order accidentals are written in, and where each sits relative to the
// first one.
//
// Convention, not arithmetic: the pattern of steps is fixed, and only its
// anchor moves with the clef. Sharps run F C G D A E B and flats run in
// reverse, so both tables are seven long and are indexed by how many
// accidentals the key has.
constexpr std::array<Step, 7> sharpOrder{
    Step::f, Step::c, Step::g, Step::d, Step::a, Step::e, Step::b};
constexpr std::array<int, 7> sharpStepOffsets{0, -3, 1, -2, -5, -1, -4};

constexpr std::array<Step, 7> flatOrder{
    Step::b, Step::e, Step::a, Step::d, Step::g, Step::c, Step::f};
constexpr std::array<int, 7> flatStepOffsets{0, 3, -1, 2, -2, 1, -3};

// The highest occurrence of a step name that still sits on or below the top
// staff line -- the anchor every key signature is built downward from.
//
// Octave arithmetic rather than a per-clef table: a step name repeats every
// seven staff steps, so the right octave is found by sliding one until it lands
// in range. That is what makes the alto clef work without anybody having
// written its key signature positions down.
[[nodiscard]] int topmostStepOf(Step name, const Clef& clef) noexcept
{
    Pitch pitch;
    pitch.step = name;
    pitch.octave = 0;

    int step = staffStepFor(pitch, clef);

    while (step + 7 <= topLineStep)
    {
        step += 7;
    }

    while (step > topLineStep)
    {
        step -= 7;
    }

    return step;
}

[[nodiscard]] Glyph timeSignatureDigit(int digit) noexcept
{
    const int clamped = std::clamp(digit, 0, 9);

    return static_cast<Glyph>(
        static_cast<std::size_t>(Glyph::timeSig0) + static_cast<std::size_t>(clamped));
}

// A number's digits, most significant first. Zero is one digit, not none.
[[nodiscard]] std::vector<int> digitsOf(int value)
{
    const std::string text = std::to_string(std::max(0, value));

    std::vector<int> digits;
    digits.reserve(text.size());

    for (const char character : text)
    {
        digits.push_back(character - '0');
    }

    return digits;
}

[[nodiscard]] double widthOfDigits(const std::vector<int>& digits)
{
    double width = 0.0;

    for (const int digit : digits)
    {
        width += advanceWidth(timeSignatureDigit(digit));
    }

    return width;
}

void placeDigits(
    const std::vector<int>& digits,
    double x,
    int staffStep,
    std::vector<PlacedGlyph>& glyphs)
{
    for (const int digit : digits)
    {
        const Glyph glyph = timeSignatureDigit(digit);

        glyphs.push_back(PlacedGlyph{glyph, x, staffStep});

        x += advanceWidth(glyph);
    }
}

// Shift a run of glyphs right, for laying a group out at zero and then placing
// it.
void offsetBy(std::vector<PlacedGlyph>& glyphs, double dx)
{
    for (PlacedGlyph& glyph : glyphs)
    {
        glyph.x += dx;
    }
}

[[nodiscard]] double rightEdgeOf(const std::vector<PlacedGlyph>& glyphs)
{
    double edge = 0.0;

    for (const PlacedGlyph& glyph : glyphs)
    {
        edge = std::max(edge, glyph.x + advanceWidth(glyph.glyph));
    }

    return edge;
}
} // namespace

ClefGlyph clefGlyphFor(const Clef& clef) noexcept
{
    ClefGlyph result;
    result.staffStep = (clef.line - 1) * 2;

    if (clef.sign.empty())
    {
        return result;
    }

    switch (clef.sign.front())
    {
    case 'G':
        result.glyph = Glyph::gClef;
        break;
    case 'F':
        result.glyph = Glyph::fClef;
        break;
    case 'C':
        result.glyph = Glyph::cClef;
        break;
    default:
        // Percussion, TAB, none. `StaffPosition.h` places their notes as if
        // treble so that something is drawn, but a key signature on a staff
        // with no pitched reading would be meaningless rather than
        // approximate.
        result.hasPitchedStaff = false;
        break;
    }

    return result;
}

std::vector<PlacedGlyph> keySignatureGlyphs(const KeySignature& key, const Clef& clef)
{
    std::vector<PlacedGlyph> glyphs;

    if (key.fifths == 0 || !clefGlyphFor(clef).hasPitchedStaff)
    {
        return glyphs;
    }

    const bool sharps = key.fifths > 0;
    const std::size_t count =
        static_cast<std::size_t>(std::min(7, sharps ? key.fifths : -key.fifths));

    const std::array<Step, 7>& order = sharps ? sharpOrder : flatOrder;
    const std::array<int, 7>& offsets = sharps ? sharpStepOffsets : flatStepOffsets;

    const int anchor = topmostStepOf(order.front(), clef);
    const Glyph glyph = sharps ? Glyph::accidentalSharp : Glyph::accidentalFlat;

    double x = 0.0;

    for (std::size_t index = 0; index < count; ++index)
    {
        glyphs.push_back(PlacedGlyph{glyph, x, anchor + offsets[index]});

        x += advanceWidth(glyph) + keyAccidentalGap;
    }

    return glyphs;
}

std::vector<PlacedGlyph> timeSignatureGlyphs(const TimeSignature& time)
{
    const std::vector<int> beats = digitsOf(time.beats);
    const std::vector<int> beatType = digitsOf(time.beatType);

    const double beatsWidth = widthOfDigits(beats);
    const double beatTypeWidth = widthOfDigits(beatType);
    const double widest = std::max(beatsWidth, beatTypeWidth);

    std::vector<PlacedGlyph> glyphs;
    glyphs.reserve(beats.size() + beatType.size());

    placeDigits(beats, (widest - beatsWidth) * 0.5, timeSignatureNumeratorStep, glyphs);
    placeDigits(beatType, (widest - beatTypeWidth) * 0.5, timeSignatureDenominatorStep, glyphs);

    return glyphs;
}

SystemHead systemHeadFor(
    const Clef& clef,
    const std::optional<KeySignature>& key,
    const std::optional<TimeSignature>& time)
{
    SystemHead head;

    const ClefGlyph clefGlyph = clefGlyphFor(clef);

    head.glyphs.push_back(PlacedGlyph{clefGlyph.glyph, headIndent, clefGlyph.staffStep});

    double x = headIndent + advanceWidth(clefGlyph.glyph);

    if (key.has_value())
    {
        std::vector<PlacedGlyph> keyGlyphs = keySignatureGlyphs(*key, clef);

        if (!keyGlyphs.empty())
        {
            offsetBy(keyGlyphs, x + headElementGap);

            x = rightEdgeOf(keyGlyphs);

            head.glyphs.insert(head.glyphs.end(), keyGlyphs.begin(), keyGlyphs.end());
        }
    }

    if (time.has_value())
    {
        std::vector<PlacedGlyph> timeGlyphs = timeSignatureGlyphs(*time);

        offsetBy(timeGlyphs, x + headElementGap);

        x = rightEdgeOf(timeGlyphs);

        head.glyphs.insert(head.glyphs.end(), timeGlyphs.begin(), timeGlyphs.end());
    }

    head.width = x + headTrailingGap;

    return head;
}
} // namespace score::engraving
