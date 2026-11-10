#include "../../../application/localisation/NoteNames.h"
#include "../../../application/theme/MiniStaff.h"
#include "TunerComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
// How the bands look on the bar and the dial: the lit one strong, the tighter
// ones inside it faint, the looser ones a dotted edge only.
constexpr float litBandAlpha = 0.85f;
constexpr float tintedBandAlpha = 0.16f;
constexpr float dottedEdgeAlpha = 0.9f;
constexpr float edgeDashes[] = {3.0f, 3.0f};

// Where the bar and the dial put their ticks and labels: both ends, the
// halfway points and zero, whatever the scale.
[[nodiscard]] std::array<double, 5> scaleTicks(double scaleCents)
{
    return {-scaleCents, -scaleCents / 2.0, 0.0, scaleCents / 2.0, scaleCents};
}

// The middle one of scaleTicks().
constexpr std::size_t zeroTick = 2;

// "+25", "-12.5", "0": a whole number of cents without a point.
[[nodiscard]] juce::String tickLabel(double cents)
{
    const auto whole = std::abs(cents - std::round(cents)) < 0.01;
    return juce::String(cents > 0.0 ? "+" : "") + juce::String(cents, whole ? 0 : 1);
}

// Whether the half of a band on `side` (-1 flat, +1 sharp) is the lit half:
// only the half the pitch is on lights, both when it is dead on the note.
//
// `bothHalves` is the tuner's "Light both halves": the symmetric look, where
// the whole zone lights whichever side the pitch is on.
[[nodiscard]] bool isLitHalf(const tuner::ZoneBand& band, double side, bool bothHalves)
{
    if (band.look != tuner::BandLook::lit)
    {
        return false;
    }
    return bothHalves || band.litSide == tuner::Side::both ||
           ((band.litSide == tuner::Side::sharp) == (side > 0.0));
}

// The sign the lit half carries, so which way to go reads at a glance: a sharp
// above the note (come down), a flat below it (go up).
[[nodiscard]] juce::String signFor(tuner::Side side)
{
    switch (side)
    {
    case tuner::Side::sharp:
        return juce::String::fromUTF8("\xE2\x99\xAF");
    case tuner::Side::flat:
        return juce::String::fromUTF8("\xE2\x99\xAD");
    case tuner::Side::both:
        break;
    }
    return {};
}

// The lit band, if any, whichever side.
[[nodiscard]] const tuner::ZoneBand* litBand(const std::vector<tuner::ZoneBand>& bands)
{
    for (const auto& band : bands)
    {
        if (band.look == tuner::BandLook::lit)
        {
            return &band;
        }
    }
    return nullptr;
}

// The lit half as a range of cents: its side of the band, or the whole band
// across zero when the pitch is dead on the note.
[[nodiscard]] std::pair<double, double> litRange(const tuner::ZoneBand& band)
{
    if (band.litSide == tuner::Side::both)
    {
        return {-band.outerCents, band.outerCents};
    }
    const double side = band.litSide == tuner::Side::sharp ? 1.0 : -1.0;
    return {side * band.innerCents, side * band.outerCents};
}

// "-18 ¢", "+3 ¢": how far off, in whole cents, with a true minus sign.
[[nodiscard]] juce::String offsetText(double cents)
{
    const auto whole = juce::roundToInt(cents);
    const auto sign = whole > 0   ? juce::String("+")
                      : whole < 0 ? juce::String::fromUTF8("\xE2\x88\x92")
                                  : juce::String();
    return sign + juce::String(std::abs(whole)) + juce::String::fromUTF8(" \xC2\xA2");
}

// The cents off, beside a sign at `anchor` when one is drawn there, or centred
// on `anchor` when not (on pitch there is no sign).
void drawOffset(
    juce::Graphics& graphics,
    juce::Point<float> anchor,
    bool besideSign,
    float signSize,
    juce::Colour colour,
    double cents)
{
    const auto size = std::max(14.0f, signSize * 0.6f);
    const juce::FontOptions font(size, juce::Font::bold);
    const auto text = offsetText(cents);
    const auto width = juce::GlyphArrangement::getStringWidth(font, text) + 4.0f;
    const auto box =
        besideSign ? juce::Rectangle<float>(
                         anchor.x + signSize * 0.8f, anchor.y - size * 0.6f, width, size * 1.2f)
                   : juce::Rectangle<float>(width, size * 1.2f).withCentre(anchor);
    graphics.setColour(colour);
    graphics.setFont(font);
    graphics.drawText(text, box, juce::Justification::centredLeft);
}

// The lit half's sign, centred on `centre` and `size` across: a sharp or
// flat, or -- with `arrows` -- an arrow the way to go, toward lower pitch when
// sharp and higher when flat. `pitchAcross` says which way pitch runs where it
// is drawn: across on the bar, the dial and a vertical graph, so the arrow
// points left or right there; up on a horizontal graph, so it points down or
// up. Drawn as a shape rather than a character, so it is the same size and
// weight whatever the font has.
void drawSign(
    juce::Graphics& graphics,
    tuner::Side side,
    juce::Point<float> centre,
    float size,
    juce::Colour colour,
    bool arrows,
    bool pitchAcross)
{
    graphics.setColour(colour);
    if (!arrows)
    {
        graphics.setFont(juce::FontOptions(size));
        graphics.drawText(
            signFor(side), juce::Rectangle<float>(size * 1.5f, size * 1.4f).withCentre(centre),
            juce::Justification::centred);
        return;
    }

    // Toward lower pitch when sharp: left across, down up the screen.
    const auto towardLower = side == tuner::Side::sharp;
    const juce::Point<float> along =
        pitchAcross ? juce::Point<float>(towardLower ? -1.0f : 1.0f, 0.0f)
                    : juce::Point<float>(0.0f, towardLower ? 1.0f : -1.0f);
    const juce::Point<float> across(-along.y, along.x);

    const auto half = size * 0.5f;
    const auto headLength = size * 0.45f;
    const auto headHalfWidth = size * 0.32f;
    const auto shaftHalfWidth = size * 0.1f;
    const auto tip = centre + along * half;
    const auto tail = centre - along * half;
    const auto neck = tip - along * headLength;

    juce::Path arrow;
    arrow.startNewSubPath(tail - across * shaftHalfWidth);
    arrow.lineTo(neck - across * shaftHalfWidth);
    arrow.lineTo(neck - across * headHalfWidth);
    arrow.lineTo(tip);
    arrow.lineTo(neck + across * headHalfWidth);
    arrow.lineTo(neck + across * shaftHalfWidth);
    arrow.lineTo(tail + across * shaftHalfWidth);
    arrow.closeSubPath();
    graphics.fillPath(arrow);
}

// Past either end of the bar or the dial: an arrow with its tip at `tip`,
// pointing along `direction` (a unit vector), off the end the reading is past.
// So a reading the scale cannot show -- two semitones off the target, say --
// still says which side it is on.
void drawOffScaleArrow(
    juce::Graphics& graphics,
    juce::Point<float> tip,
    juce::Point<float> direction,
    juce::Colour fill,
    juce::Colour edge)
{
    const juce::Point<float> across(-direction.y, direction.x);
    const auto base = tip - direction * 20.0f;

    juce::Path arrow;
    arrow.addTriangle(tip, base + across * 12.0f, base - across * 12.0f);
    graphics.setColour(fill);
    graphics.fillPath(arrow);
    graphics.setColour(edge);
    graphics.strokePath(arrow, juce::PathStrokeType(1.5f));
}

// The note name drawn behind a display, sized from the area it is given so
// it fills a tall docked pane and still fits the band the bar leaves it. The
// cap only stops it becoming absurd on a very large display.
[[nodiscard]] float noteFontHeight(juce::Rectangle<int> area)
{
    return juce::jlimit(24.0f, 360.0f, static_cast<float>(area.getHeight()) * 0.28f);
}

// Red covers everything past the out-of-tune limit -- on a graph, most of the
// plot -- so it is kept very faint when lit, and invisible otherwise: when the
// pitch is not in it (tuner::BandLook::hidden), and on the half of it the
// pitch is not on.
constexpr float outsideLitAlpha = 0.14f;
constexpr float outsideTintedAlpha = 0.0f;
constexpr float graphOutsideLitAlpha = 0.09f;
constexpr float graphOutsideTintedAlpha = 0.0f;

// How strongly one half of a band is filled.
[[nodiscard]] float fillAlpha(
    const tuner::ZoneBand& band,
    double side,
    bool bothHalves,
    float lit,
    float tinted,
    float outsideLit,
    float outsideTinted)
{
    const auto isLit = isLitHalf(band, side, bothHalves);
    if (band.zone == pitch::IntonationZone::outside)
    {
        return isLit ? outsideLit : outsideTinted;
    }
    return isLit ? lit : tinted;
}

// On a graph the trace runs across the bands, so they are quieter there.
constexpr float graphLitBandAlpha = 0.32f;
constexpr float graphTintedBandAlpha = 0.10f;

// The zones as strips around `note` across a pitch graph, whose pitch axis
// runs `lowest` to `highest` in semitones: up the plot, or across it when
// `vertical` (a vertical pane runs time down and pitch across). Fills first,
// then a dotted line where each looser zone begins. Clipped to the plot, so a band past the
// range is cut off rather than drawn over the labels.
void drawZoneStrips(
    juce::Graphics& graphics,
    const std::vector<tuner::ZoneBand>& bands,
    juce::Rectangle<float> plot,
    double note,
    double lowest,
    double highest,
    bool vertical,
    bool bothHalves,
    bool showSigns,
    bool arrowSigns,
    float signSize,
    bool showOffset,
    double cents)
{
    const juce::Graphics::ScopedSaveState state(graphics);
    graphics.reduceClipRegion(plot.getSmallestIntegerContainer());

    const auto at = [&](double unclamped)
    {
        const auto midi = juce::jlimit(lowest, highest, unclamped);
        return vertical ? juce::jmap(
                              static_cast<float>(midi), static_cast<float>(lowest),
                              static_cast<float>(highest), plot.getX(), plot.getRight())
                        : juce::jmap(
                              static_cast<float>(midi), static_cast<float>(lowest),
                              static_cast<float>(highest), plot.getBottom(), plot.getY());
    };

    for (auto band = bands.rbegin(); band != bands.rend(); ++band)
    {
        if (band->look == tuner::BandLook::dotted || band->look == tuner::BandLook::hidden)
        {
            continue;
        }
        for (const double side : {-1.0, 1.0})
        {
            graphics.setColour(
                intonationZoneColour(band->zone)
                    .withAlpha(fillAlpha(
                        *band, side, bothHalves, graphLitBandAlpha, graphTintedBandAlpha,
                        graphOutsideLitAlpha, graphOutsideTintedAlpha)));
            const auto from = at(note + side * band->innerCents / 100.0);
            const auto to = at(note + side * band->outerCents / 100.0);
            graphics.fillRect(
                vertical
                    ? juce::Rectangle<float>::leftTopRightBottom(
                          std::min(from, to), plot.getY(), std::max(from, to), plot.getBottom())
                    : juce::Rectangle<float>::leftTopRightBottom(
                          plot.getX(), std::min(from, to), plot.getRight(), std::max(from, to)));
        }
    }

    for (const auto& band : bands)
    {
        if (band.look != tuner::BandLook::dotted)
        {
            continue;
        }
        graphics.setColour(intonationZoneColour(band.zone).withAlpha(dottedEdgeAlpha));
        for (const double side : {-1.0, 1.0})
        {
            const auto edge = at(note + side * band.innerCents / 100.0);
            graphics.drawDashedLine(
                vertical ? juce::Line<float>(edge, plot.getY(), edge, plot.getBottom())
                         : juce::Line<float>(plot.getX(), edge, plot.getRight(), edge),
                edgeDashes, 2, 1.5f);
        }
    }

    // The sign, in the lit half, near the start of the plot where the trace is
    // oldest and least in the way.
    if (const auto* lit = litBand(bands); lit != nullptr)
    {
        const auto [low, high] = litRange(*lit);
        const auto middle = (at(note + low / 100.0) + at(note + high / 100.0)) * 0.5f;
        const auto anchor = vertical ? juce::Point<float>(middle, plot.getY() + signSize)
                                     : juce::Point<float>(plot.getX() + signSize, middle);
        const auto signDrawn = showSigns && tuner::carriesSign(*lit);
        if (signDrawn)
        {
            drawSign(
                graphics, lit->litSide, anchor, signSize, intonationZoneColour(lit->zone),
                arrowSigns, vertical);
        }
        if (showOffset)
        {
            drawOffset(
                graphics, anchor, signDrawn, signSize, intonationZoneColour(lit->zone), cents);
        }
    }
}

// The graph's axis labels and the status line's note name must agree: both are
// l10n::heardNoteName, in the reader's naming and spelled for the score's key.
[[nodiscard]] juce::String noteLabelForMidi(int midiNote, int keyFifths)
{
    return l10n::heardNoteName(midiNote, keyFifths);
}
} // namespace

//==============================================================================
// Zones

pitch::ZoneLimits TunerComponent::zoneLimits() const
{
    return limits ? pitch::orderedLimits(limits()) : pitch::ZoneLimits{};
}

std::optional<int> TunerComponent::targetMidi() const
{
    // Not centring on it leaves the tuner reading against the nearest note,
    // as it does with no target at all.
    if (!targetCentred)
    {
        return std::nullopt;
    }
    const auto aimingAt = target.target();
    return aimingAt != nullptr ? std::optional<int>(aimingAt->midiNoteNumber) : std::nullopt;
}

double TunerComponent::zoneCents() const
{
    return hasLockedMidiNote ? tuner::centsAgainst(targetMidi(), lockedMidiNote, displayedCents)
                             : displayedCents;
}

std::optional<int> TunerComponent::zoneNote() const
{
    if (const auto aimed = targetMidi())
    {
        return aimed;
    }
    return hasLockedMidiNote ? std::optional<int>(lockedMidiNote) : std::nullopt;
}

bool TunerComponent::isNearPerfect() const
{
    return std::abs(zoneCents()) <= zoneLimits().nearPerfectCents;
}

std::vector<tuner::ZoneBand> TunerComponent::currentBands() const
{
    return tuner::zoneBands(
        hasSignal ? std::optional<double>(zoneCents()) : std::nullopt, zoneLimits());
}

//==============================================================================
// Drawing

void TunerComponent::drawPitchGraph(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);

    auto content = bounds.reduced(8);
    auto labelArea = content.removeFromLeft(scaledHeight(48));
    const auto plotArea = content;

    std::vector<double> validValues;
    validValues.reserve(graphHistory.size());
    for (const auto value : graphHistory)
    {
        if (std::isfinite(value))
        {
            validValues.push_back(value);
        }
    }

    double minimumValue = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) - 3.0 : 66.0;
    double maximumValue = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) + 3.0 : 72.0;

    if (!validValues.empty())
    {
        minimumValue = *std::min_element(validValues.begin(), validValues.end()) - 0.5;
        maximumValue = *std::max_element(validValues.begin(), validValues.end()) + 0.5;

        if (maximumValue - minimumValue < 6.0)
        {
            const auto centre = (minimumValue + maximumValue) * 0.5;
            minimumValue = centre - 3.0;
            maximumValue = centre + 3.0;
        }

        // The zones around the note being read stay on the graph while there
        // is a reading, so the trace can be seen against where it should be.
        // Only while there is a signal: a lock left over from a pause must
        // not hold the axis open.
        if (const auto centre = zoneNote(); hasSignal && centre.has_value())
        {
            const auto halfBand = pitch::nearNoteCents / 100.0;
            minimumValue = std::min(minimumValue, *centre - halfBand);
            maximumValue = std::max(maximumValue, *centre + halfBand);
        }

        // Snapped to whole semitones, for the same reason the compact graph
        // does it: the gridlines below are drawn at integer semitones within
        // this range, so ends that move by a fraction of one every frame --
        // which a data-driven min/max does continuously, both as the trace
        // moves and as points scroll off the end -- slide the entire grid
        // under the trace. A held note then appears to drift against lines
        // that are themselves drifting, and neither can be read against the
        // other.
        minimumValue = std::floor(minimumValue);
        maximumValue = std::ceil(maximumValue);
    }

    // Centred on the target, when there is one and the tuner centres on it:
    // its line in the middle, reaching far enough either way for the whole
    // trace and never less than three semitones. Symmetric whole semitones
    // round a whole-semitone target, so it stays still as the trace moves.
    if (const auto aimed = targetMidi())
    {
        double reach = 3.0;
        for (const auto value : validValues)
        {
            reach = std::max(reach, std::abs(value - *aimed) + 0.5);
        }
        minimumValue = std::floor(*aimed - reach);
        maximumValue = std::ceil(*aimed + reach);
    }

    const auto firstNote = static_cast<int>(std::ceil(minimumValue));
    const auto lastNote = static_cast<int>(std::floor(maximumValue));
    const auto pixelsPerSemitone =
        static_cast<double>(plotArea.getHeight()) / std::max(1.0, maximumValue - minimumValue);

    graphics.setFont(textFont(appearance::TextRole::axis));

    for (int midiNote = firstNote; midiNote <= lastNote; ++midiNote)
    {
        if (!showsNoteRow(midiNote))
        {
            continue;
        }
        const auto y = juce::jmap(
            static_cast<float>(midiNote), static_cast<float>(minimumValue),
            static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
            static_cast<float>(plotArea.getY()));

        const auto isCurrentNote = hasSignal && midiNote == lockedMidiNote;
        const auto isC = ((midiNote % 12) + 12) % 12 == 0;

        graphics.setColour(
            isCurrentNote ? palette.accent.withAlpha(0.48f)
            : isC         ? palette.muted.withAlpha(0.30f)
                          : palette.outline.withAlpha(0.62f));
        graphics.drawHorizontalLine(
            static_cast<int>(std::round(y)), static_cast<float>(plotArea.getX()),
            static_cast<float>(plotArea.getRight()));

        if (pixelsPerSemitone >= 10.0 || isCurrentNote || isC)
        {
            graphics.setColour(isCurrentNote ? palette.accent : palette.muted);
            graphics.drawFittedText(
                noteLabelForMidi(midiNote, target.keyFifths),
                labelArea.withY(static_cast<int>(std::round(y)) - 8).withHeight(16),
                juce::Justification::centredRight, 1);
        }
    }

    if (const auto centre = zoneNote())
    {
        drawZoneStrips(
            graphics, currentBands(), plotArea.toFloat(), *centre, minimumValue, maximumValue,
            false, bothHalvesLit, signs != Signs::off, signs == Signs::arrows, signHeight(bounds),
            offsetShown, zoneCents());
    }

    if (graphHistory.size() < 2 || validValues.empty())
    {
        return;
    }

    juce::Path pitchPath;
    bool pathHasStarted = false;

    for (std::size_t index = 0; index < graphHistory.size(); ++index)
    {
        const auto value = graphHistory[index];
        if (!std::isfinite(value))
        {
            pathHasStarted = false;
            continue;
        }

        const auto x = juce::jmap(
            static_cast<float>(index), 0.0f, static_cast<float>(graphHistory.size() - 1),
            static_cast<float>(plotArea.getX()), static_cast<float>(plotArea.getRight()));
        const auto y = juce::jmap(
            static_cast<float>(value), static_cast<float>(minimumValue),
            static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
            static_cast<float>(plotArea.getY()));

        if (pathHasStarted)
        {
            pitchPath.lineTo(x, y);
        }
        else
        {
            pitchPath.startNewSubPath(x, y);
            pathHasStarted = true;
        }
    }

    graphics.setColour(palette.accent);
    graphics.strokePath(pitchPath, juce::PathStrokeType(2.0f));

    // Where the pitch is now: the newest point.
    if (std::isfinite(graphHistory.back()))
    {
        const auto y = juce::jmap(
            static_cast<float>(graphHistory.back()), static_cast<float>(minimumValue),
            static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
            static_cast<float>(plotArea.getY()));
        drawMarker(
            graphics, {static_cast<float>(plotArea.getRight()), y}, plotArea.toFloat(), false);
    }
}

void TunerComponent::drawPitchBar(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);

    // How far either way it reaches, from the tuner's ... menu: ±50 cents, one
    // note, unless changed. A reading past the end sits at the end.
    const auto scaleCents = scaleSlider.getValue();

    // Half the height of the note name above the bar.
    const auto signSize = signHeight(bounds);

    auto barBounds = bounds.reduced(28, 34);
    const auto centreY = barBounds.getCentreY();

    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(
        barBounds.toFloat().withHeight(10.0f).withCentre(
            {static_cast<float>(barBounds.getCentreX()), static_cast<float>(centreY)}),
        5.0f);

    // The zones, each as wide as its range, loosest first so the tighter ones
    // sit on top.
    const auto xOf = [&barBounds, scaleCents](double cents)
    {
        return juce::jmap(
            static_cast<float>(juce::jlimit(-scaleCents, scaleCents, cents)),
            static_cast<float>(-scaleCents), static_cast<float>(scaleCents),
            static_cast<float>(barBounds.getX()), static_cast<float>(barBounds.getRight()));
    };
    const auto bandTop = static_cast<float>(centreY) - 14.0f;
    const auto bandBottom = static_cast<float>(centreY) + 14.0f;
    const auto bands = currentBands();
    for (auto band = bands.rbegin(); band != bands.rend(); ++band)
    {
        const auto colour = intonationZoneColour(band->zone);
        for (const double side : {-1.0, 1.0})
        {
            if (band->look == tuner::BandLook::dotted || band->look == tuner::BandLook::hidden)
            {
                continue;
            }
            const auto from = xOf(side * band->innerCents);
            const auto to = xOf(side * band->outerCents);
            graphics.setColour(colour.withAlpha(fillAlpha(
                *band, side, bothHalvesLit, litBandAlpha, tintedBandAlpha, outsideLitAlpha,
                outsideTintedAlpha)));
            graphics.fillRect(juce::Rectangle<float>::leftTopRightBottom(
                std::min(from, to), bandTop, std::max(from, to), bandBottom));
        }
    }

    graphics.setFont(textFont(appearance::TextRole::small));
    const auto ticks = scaleTicks(scaleCents);
    for (std::size_t tick = 0; tick < ticks.size(); ++tick)
    {
        const auto cents = ticks[tick];
        const auto x = xOf(cents);

        graphics.setColour(tick == zeroTick ? palette.foreground : palette.muted);
        graphics.drawVerticalLine(
            static_cast<int>(std::round(x)), static_cast<float>(centreY) - 18.0f,
            static_cast<float>(centreY) + 18.0f);
        graphics.drawText(
            tickLabel(cents), static_cast<int>(x) - 24, centreY + 22, 48, 18,
            juce::Justification::centred);
    }

    // Where each looser zone begins, over the ticks so a line on a tick
    // still shows.
    for (const auto& band : bands)
    {
        if (band.look != tuner::BandLook::dotted)
        {
            continue;
        }
        graphics.setColour(intonationZoneColour(band.zone).withAlpha(dottedEdgeAlpha));
        for (const double side : {-1.0, 1.0})
        {
            const auto x = xOf(side * band.innerCents);
            graphics.drawDashedLine(
                {x, bandTop - 10.0f, x, bandBottom + 10.0f}, edgeDashes, 2, 2.0f);
        }
    }

    if (!hasSignal)
    {
        return;
    }

    const auto indicatorX = xOf(zoneCents());

    // The sign over the lit half: a sharp to come down from, a flat to go up
    // from. Over the band rather than on it, so the dot never hides it.
    if (const auto* lit = litBand(bands); lit != nullptr)
    {
        const auto [low, high] = litRange(*lit);
        const juce::Point<float> anchor{(xOf(low) + xOf(high)) * 0.5f, bandTop - signSize * 0.8f};
        const auto signDrawn = signs != Signs::off && tuner::carriesSign(*lit);
        if (signDrawn)
        {
            drawSign(
                graphics, lit->litSide, anchor, signSize, intonationZoneColour(lit->zone),
                signs == Signs::arrows, true);
        }
        if (offsetShown)
        {
            drawOffset(
                graphics, anchor, signDrawn, signSize, intonationZoneColour(lit->zone),
                zoneCents());
        }
    }

    // The chosen marker: a dot on the bar, an arrow down onto it, or a line
    // across the band.
    drawMarker(
        graphics, {indicatorX, static_cast<float>(centreY)},
        juce::Rectangle<float>::leftTopRightBottom(
            indicatorX, bandTop - 10.0f, indicatorX, bandBottom + 10.0f),
        true);

    if (const auto side = tuner::offScale(zoneCents(), scaleCents); side != 0)
    {
        const auto end = static_cast<float>(side > 0 ? barBounds.getRight() : barBounds.getX());
        const auto outward = static_cast<float>(side);
        drawOffScaleArrow(
            graphics, {end + outward * 24.0f, static_cast<float>(centreY)}, {outward, 0.0f},
            intonationZoneColour(pitch::zoneOf(zoneCents(), zoneLimits())), palette.foreground);
    }
}

void TunerComponent::drawPitchMeter(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);
    const auto scaleCents = scaleSlider.getValue();

    // Half the height of the note name behind the dial.
    const auto signSize = signHeight(bounds);

    const auto centre = juce::Point<float>(
        static_cast<float>(bounds.getCentreX()), static_cast<float>(bounds.getBottom() - 24));
    const auto radius = static_cast<float>(
        std::max(30, std::min(bounds.getWidth() / 2 - 28, bounds.getHeight() - 54)));

    constexpr double startAngle = -2.45;
    constexpr double endAngle = -0.69;

    juce::Path arc;
    for (int step = 0; step <= 64; ++step)
    {
        const auto proportion = static_cast<double>(step) / 64.0;
        const auto angle = startAngle + proportion * (endAngle - startAngle);
        const auto point = centre + juce::Point<float>(
                                        static_cast<float>(std::cos(angle) * radius),
                                        static_cast<float>(std::sin(angle) * radius));

        if (step == 0)
        {
            arc.startNewSubPath(point);
        }
        else
        {
            arc.lineTo(point);
        }
    }

    graphics.setColour(palette.outline);
    graphics.strokePath(arc, juce::PathStrokeType(5.0f));

    // The zones as arcs just inside the rim, each as wide as its range, and a
    // dotted radial line where each looser one begins.
    const auto angleOf = [scaleCents](double cents)
    {
        return juce::jmap(
            juce::jlimit(-scaleCents, scaleCents, cents), -scaleCents, scaleCents, startAngle,
            endAngle);
    };
    const auto pointAt = [&centre](double angle, float distance)
    {
        return centre + juce::Point<float>(
                            static_cast<float>(std::cos(angle) * distance),
                            static_cast<float>(std::sin(angle) * distance));
    };
    const auto bandRadius = radius - 9.0f;
    const auto bands = currentBands();
    for (auto band = bands.rbegin(); band != bands.rend(); ++band)
    {
        const auto colour = intonationZoneColour(band->zone);
        for (const double side : {-1.0, 1.0})
        {
            if (band->look == tuner::BandLook::dotted || band->look == tuner::BandLook::hidden)
            {
                continue;
            }
            const auto from = angleOf(side * band->innerCents);
            const auto to = angleOf(side * band->outerCents);
            juce::Path segment;
            constexpr int steps = 24;
            for (int step = 0; step <= steps; ++step)
            {
                const auto angle = from + (to - from) * static_cast<double>(step) / steps;
                if (step == 0)
                {
                    segment.startNewSubPath(pointAt(angle, bandRadius));
                }
                else
                {
                    segment.lineTo(pointAt(angle, bandRadius));
                }
            }
            graphics.setColour(colour.withAlpha(fillAlpha(
                *band, side, bothHalvesLit, litBandAlpha, tintedBandAlpha, outsideLitAlpha,
                outsideTintedAlpha)));
            graphics.strokePath(
                segment, juce::PathStrokeType(
                             14.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
    }

    graphics.setFont(textFont(appearance::TextRole::small));
    const auto ticks = scaleTicks(scaleCents);
    for (std::size_t tick = 0; tick < ticks.size(); ++tick)
    {
        const auto angle = angleOf(ticks[tick]);
        const auto outerPoint = centre + juce::Point<float>(
                                             static_cast<float>(std::cos(angle) * radius),
                                             static_cast<float>(std::sin(angle) * radius));
        const auto innerPoint =
            centre + juce::Point<float>(
                         static_cast<float>(std::cos(angle) * (radius - 15.0f)),
                         static_cast<float>(std::sin(angle) * (radius - 15.0f)));

        graphics.setColour(tick == zeroTick ? palette.foreground : palette.muted);
        graphics.drawLine({innerPoint, outerPoint}, tick == zeroTick ? 2.0f : 1.0f);
    }

    // Where each looser zone begins, over the rim and the ticks.
    for (const auto& band : bands)
    {
        if (band.look != tuner::BandLook::dotted)
        {
            continue;
        }
        graphics.setColour(intonationZoneColour(band.zone).withAlpha(dottedEdgeAlpha));
        for (const double side : {-1.0, 1.0})
        {
            const auto angle = angleOf(side * band.innerCents);
            graphics.drawDashedLine(
                {pointAt(angle, radius - 30.0f), pointAt(angle, radius + 10.0f)}, edgeDashes, 2,
                2.0f);
        }
    }

    // The sign just inside the lit half of the arc.
    if (const auto* lit = litBand(bands); lit != nullptr)
    {
        const auto [low, high] = litRange(*lit);
        const auto anchor =
            pointAt((angleOf(low) + angleOf(high)) * 0.5, radius - 18.0f - signSize);
        const auto signDrawn = signs != Signs::off && tuner::carriesSign(*lit);
        if (signDrawn)
        {
            drawSign(
                graphics, lit->litSide, anchor, signSize, intonationZoneColour(lit->zone),
                signs == Signs::arrows, true);
        }
        if (offsetShown)
        {
            drawOffset(
                graphics, anchor, signDrawn, signSize, intonationZoneColour(lit->zone),
                zoneCents());
        }
    }

    const auto needleAngle = angleOf(hasSignal ? zoneCents() : 0.0);
    const auto needleEnd =
        centre + juce::Point<float>(
                     static_cast<float>(std::cos(needleAngle) * (radius - 20.0f)),
                     static_cast<float>(std::sin(needleAngle) * (radius - 20.0f)));

    graphics.setColour(
        hasSignal ? intonationZoneColour(pitch::zoneOf(zoneCents(), zoneLimits())) : palette.muted);
    graphics.drawLine({centre, needleEnd}, 3.0f);
    graphics.fillEllipse(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);

    // Past an end of the dial: an arrow there, carrying on along the arc.
    if (const auto side = tuner::offScale(zoneCents(), scaleCents); hasSignal && side != 0)
    {
        const auto angle = angleOf(side * scaleCents);
        const auto outward = static_cast<float>(side);
        const juce::Point<float> along(
            -static_cast<float>(std::sin(angle)) * outward,
            static_cast<float>(std::cos(angle)) * outward);
        drawOffScaleArrow(
            graphics, pointAt(angle, radius) + along * 26.0f, along,
            intonationZoneColour(pitch::zoneOf(zoneCents(), zoneLimits())), palette.foreground);
    }

    graphics.setColour(palette.muted);
    graphics.setFont(textFont(appearance::TextRole::small));
    graphics.drawText(
        l10n::text("tuner.meter.flat"),
        bounds.removeFromLeft(scaledHeight(74)).removeFromBottom(scaledHeight(24)),
        juce::Justification::centred);
    graphics.drawText(
        l10n::text("tuner.meter.sharp"),
        bounds.removeFromRight(scaledHeight(74)).removeFromBottom(scaledHeight(24)),
        juce::Justification::centred);
}

void TunerComponent::drawNoteWatermark(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    // The note being played is drawn *behind* the display rather than above it,
    // at low opacity. It reads at a glance from across a room without spending
    // any vertical space, which is what lets the display have the rest of the
    // panel -- the note used to take 96px off the top before the graph got
    // anything at all.
    //
    // Nothing is drawn without a signal: displayedNote is "--" then, and a
    // giant translucent "--" reads as a rendering fault rather than as an idle
    // tuner. The status line above already says what to do.
    if (!hasSignal)
    {
        return;
    }

    const auto palette = tunerPaletteFor(currentTheme);

    const auto fontHeight = noteFontHeight(area);

    graphics.setColour(palette.foreground.withAlpha(0.14f));
    graphics.setFont(juce::FontOptions(fontHeight, juce::Font::bold));
    graphics.drawFittedText(displayedNote, area, juce::Justification::centred, 1);
}

void TunerComponent::drawCompactGraph(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    // The pitch trace along whichever axis has room, and a scale coarse enough
    // to still be a scale. The full graph draws a gridline per semitone over
    // six semitones; at 90px that is a line every 15px with a label on each,
    // which reads as texture rather than as pitch. So the range narrows and the
    // labels thin out as the space does.
    const auto palette = tunerPaletteFor(currentTheme);
    const auto vertical = shape == compact::Shape::vertical;
    const auto alongAxis = vertical ? bounds.getHeight() : bounds.getWidth();
    const auto detail = compact::detailFor(alongAxis);

    // Two semitones when there is almost no room, up to six when there is.
    const auto span = 2.0 + 4.0 * static_cast<double>(detail);

    // The trace decides the range whenever there is a trace. The locked note
    // is only a fallback, for a graph with nothing in it yet.
    //
    // These used to be unioned -- a window around the locked note, widened by
    // the data -- which tied the axis to a value that disappears. Losing the
    // lock during a pause dropped `centreNote` to its A4 default while the
    // history still held, say, an E3 an octave and a half below, so the range
    // stretched to cover both and the graph went from eight semitones to
    // twenty at the moment the singing stopped. Nothing about the trace had
    // changed; only its anchor had gone.
    auto hasData = false;
    auto lowest = 0.0;
    auto highest = 0.0;

    for (const auto value : graphHistory)
    {
        if (!std::isfinite(value))
        {
            continue;
        }

        lowest = hasData ? std::min(lowest, value) : value;
        highest = hasData ? std::max(highest, value) : value;
        hasData = true;
    }

    if (!hasData)
    {
        const auto centreNote = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) : 69.0;
        lowest = centreNote - span * 0.5;
        highest = centreNote + span * 0.5;
    }
    else if (highest - lowest < span)
    {
        // `span` as a floor, so a dead-steady note does not get an axis zoomed
        // to its own jitter -- centred on the trace, not on the locked note,
        // so this cannot move the window either.
        const auto centre = (lowest + highest) * 0.5;
        lowest = centre - span * 0.5;
        highest = centre + span * 0.5;
    }

    // The zones around the note being read, kept in range while there is a
    // reading -- only then, for the reason above: a lock that outlives the
    // singing must not hold the axis.
    if (const auto centre = zoneNote(); hasData && hasSignal && centre.has_value())
    {
        const auto halfBand = pitch::nearNoteCents / 100.0;
        lowest = std::min(lowest, *centre - halfBand);
        highest = std::max(highest, *centre + halfBand);
    }

    // Air in whole semitones, and the ends snapped to them.
    //
    // The gridlines are drawn at integer semitones *inside* this range, so
    // where the ends fall decides where every line lands. A proportional
    // margin over a min/max that changes every frame -- as the trace moves and
    // as old points scroll off the end -- moves those ends by a fraction of a
    // semitone continuously, which slides the whole grid underneath a trace
    // that ought to be sitting still against it. The pitch stops being
    // readable against a line that will not hold still, and a note held dead
    // steady still appears to drift.
    //
    // Quantised, the range is exactly the same from frame to frame for as long
    // as the trace stays inside it, and steps by one whole line when it does
    // not -- so a line means one fixed pitch for as long as it is on screen.
    // Centred on the target, as the full graph is, when it centres on one.
    if (const auto aimed = targetMidi())
    {
        double reach = span * 0.5;
        if (hasData)
        {
            reach = std::max({reach, *aimed - lowest, highest - *aimed});
        }
        lowest = *aimed - reach;
        highest = *aimed + reach;
    }

    lowest = std::floor(lowest) - 1.0;
    highest = std::ceil(highest) + 1.0;

    const auto plot = bounds.reduced(6);

    // The current note's line, always. It is the one the trace is read against.
    for (int midiNote = static_cast<int>(std::ceil(lowest));
         midiNote <= static_cast<int>(std::floor(highest)); ++midiNote)
    {
        const auto isCurrent = hasSignal && midiNote == lockedMidiNote;

        // Every gridline when there is room; only the current one when there
        // is not.
        if (!isCurrent && (detail < 0.35f || !showsNoteRow(midiNote)))
        {
            continue;
        }

        graphics.setColour(
            isCurrent ? palette.accent.withAlpha(0.48f) : palette.outline.withAlpha(0.62f));

        if (vertical)
        {
            const auto x = juce::jmap(
                static_cast<float>(midiNote), static_cast<float>(lowest),
                static_cast<float>(highest), static_cast<float>(plot.getX()),
                static_cast<float>(plot.getRight()));
            graphics.drawVerticalLine(
                static_cast<int>(std::round(x)), static_cast<float>(plot.getY()),
                static_cast<float>(plot.getBottom()));
        }
        else
        {
            const auto y = juce::jmap(
                static_cast<float>(midiNote), static_cast<float>(lowest),
                static_cast<float>(highest), static_cast<float>(plot.getBottom()),
                static_cast<float>(plot.getY()));
            graphics.drawHorizontalLine(
                static_cast<int>(std::round(y)), static_cast<float>(plot.getX()),
                static_cast<float>(plot.getRight()));
        }
    }

    if (const auto centre = zoneNote())
    {
        drawZoneStrips(
            graphics, currentBands(), plot.toFloat(), *centre, lowest, highest, vertical,
            bothHalvesLit, signs != Signs::off, signs == Signs::arrows,
            std::min(signHeight(bounds), 0.3f * static_cast<float>(plot.getHeight())), offsetShown,
            zoneCents());
    }

    if (graphHistory.size() >= 2)
    {
        juce::Path trace;
        bool started = false;

        for (std::size_t index = 0; index < graphHistory.size(); ++index)
        {
            const auto value = graphHistory[index];

            if (!std::isfinite(value))
            {
                started = false;
                continue;
            }

            // Time runs down a vertical pane and across a horizontal one; pitch
            // takes the other axis in both.
            const auto alongTime = juce::jmap(
                static_cast<float>(index), 0.0f, static_cast<float>(graphHistory.size() - 1),
                static_cast<float>(vertical ? plot.getY() : plot.getX()),
                static_cast<float>(vertical ? plot.getBottom() : plot.getRight()));
            // Clamped as well as ranged. The range above covers every finite
            // value, so this only catches the degenerate case where they are
            // all equal -- but a trace outside the panel is exactly the bug
            // being fixed, and it should not depend on arithmetic elsewhere
            // staying correct.
            const auto alongPitch = juce::jmap(
                static_cast<float>(juce::jlimit(lowest, highest, value)),
                static_cast<float>(lowest), static_cast<float>(highest),
                static_cast<float>(vertical ? plot.getX() : plot.getBottom()),
                static_cast<float>(vertical ? plot.getRight() : plot.getY()));

            const auto x = vertical ? alongPitch : alongTime;
            const auto y = vertical ? alongTime : alongPitch;

            started ? trace.lineTo(x, y) : trace.startNewSubPath(x, y);
            started = true;
        }

        graphics.setColour(palette.accent);
        graphics.strokePath(trace, juce::PathStrokeType(2.0f));

        if (started)
        {
            drawMarker(graphics, trace.getCurrentPosition(), plot.toFloat(), vertical);
        }
    }

    drawCompactReading(
        graphics, juce::Rectangle<int>(0, 0, juce::jmin(bounds.getWidth() - 12, 160), 44)
                      .withCentre(bounds.getCentre()));
}

void TunerComponent::drawCompactBar(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    // A bar along the long axis with the reading in the middle of it. The
    // tick labels go: five numbers do not fit across 180px, and the position
    // of the marker already carries what they said.
    const auto palette = tunerPaletteFor(currentTheme);
    const auto vertical = shape == compact::Shape::vertical;
    const auto inTune = isNearPerfect();
    const auto plot = bounds.reduced(10);

    constexpr float trackThickness = 8.0f;
    const auto centre = plot.getCentre();

    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(
        vertical ? juce::Rectangle<float>(
                       static_cast<float>(centre.x) - trackThickness * 0.5f,
                       static_cast<float>(plot.getY()), trackThickness,
                       static_cast<float>(plot.getHeight()))
                 : juce::Rectangle<float>(
                       static_cast<float>(plot.getX()),
                       static_cast<float>(centre.y) - trackThickness * 0.5f,
                       static_cast<float>(plot.getWidth()), trackThickness),
        trackThickness * 0.5f);

    // The centre line, so "in tune" is a place rather than only a colour.
    graphics.setColour(palette.foreground.withAlpha(0.5f));

    if (vertical)
    {
        graphics.drawHorizontalLine(
            centre.y, static_cast<float>(centre.x) - 14.0f, static_cast<float>(centre.x) + 14.0f);
    }
    else
    {
        graphics.drawVerticalLine(
            centre.x, static_cast<float>(centre.y) - 14.0f, static_cast<float>(centre.y) + 14.0f);
    }

    if (hasSignal)
    {
        // The same scale as the full bar, from the ... menu, so a reading sits
        // in the same place whichever size the tuner is.
        const auto scale = static_cast<float>(scaleSlider.getValue());
        const auto cents = juce::jlimit(-scale, scale, static_cast<float>(zoneCents()));

        // Sharp is up on a vertical bar and right on a horizontal one, which is
        // the direction each of those already means elsewhere in the tool.
        const auto position =
            vertical ? juce::jmap(
                           cents, -scale, scale, static_cast<float>(plot.getBottom()),
                           static_cast<float>(plot.getY()))
                     : juce::jmap(
                           cents, -scale, scale, static_cast<float>(plot.getX()),
                           static_cast<float>(plot.getRight()));

        graphics.setColour(inTune ? palette.inTune : palette.accent);
        graphics.fillEllipse(
            (vertical ? static_cast<float>(centre.x) : position) - 9.0f,
            (vertical ? position : static_cast<float>(centre.y)) - 9.0f, 18.0f, 18.0f);
    }

    // Clear of the track, not centred on it. Centred, the marker sits exactly
    // on the note whenever the reading is in tune -- which is the moment the
    // note most wants reading. Offset perpendicular to the track, the two never
    // meet at any reading.
    const int readingHeight = scaledHeight(34);
    constexpr int clearance = 16;
    const auto readingCentre =
        vertical ? juce::Point<int>(
                       centre.x - static_cast<int>(trackThickness) - clearance - readingHeight / 2,
                       centre.y)
                 : juce::Point<int>(
                       centre.x,
                       centre.y - static_cast<int>(trackThickness) - clearance - readingHeight / 2);

    drawCompactReading(
        graphics, juce::Rectangle<int>(0, 0, juce::jmin(bounds.getWidth() - 12, 120), readingHeight)
                      .withCentre(readingCentre));
}

void TunerComponent::drawCompactMeter(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    // The one display that does not rotate. A dial needs width *and* height and
    // has neither here, so it becomes what it was actually telling you: the
    // note, and how close you are, carried by colour.
    juce::ignoreUnused(shape);

    const auto palette = tunerPaletteFor(currentTheme);
    const auto inTune = isNearPerfect();
    auto content = bounds.reduced(8);

    if (hasSignal)
    {
        // The whole face takes the colour, so it reads from across a room at a
        // size where no text would.
        // Darker on the light palette: 0.18 over white is barely a tint, and
        // the wash is the whole signal at this size.
        graphics.setColour((inTune ? palette.inTune : palette.accent)
                               .withAlpha(isDarkTheme(currentTheme) ? 0.18f : 0.30f));
        graphics.fillRoundedRectangle(bounds.reduced(2).toFloat(), 6.0f);
    }

    auto directionArea = content.removeFromBottom(std::min(28, content.getHeight() / 4));

    graphics.setColour(hasSignal ? palette.foreground : palette.muted);
    graphics.setFont(juce::FontOptions(
        juce::jlimit(24.0f, 190.0f, static_cast<float>(content.getHeight()) * 0.55f),
        juce::Font::bold));
    graphics.drawFittedText(
        hasSignal ? displayedNote : juce::String("--"), content, juce::Justification::centred, 1);

    if (!hasSignal)
    {
        return;
    }

    graphics.setColour(inTune ? palette.inTune : palette.accent);
    graphics.setFont(textFont(appearance::TextRole::control, juce::Font::bold));
    graphics.drawFittedText(
        inTune ? l10n::text("tuner.direction.in-tune") : directionLabel(), directionArea,
        juce::Justification::centred, 1);
}

juce::String TunerComponent::directionLabel() const
{
    return zoneCents() > 0 ? l10n::text("tuner.direction.sharp")
                           : l10n::text("tuner.direction.flat");
}

void TunerComponent::drawCompactReading(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    // The note, as large as the space allows, with a small arrow beside it when
    // the reading is off. The arrow is the whole point of a tuner at this size:
    // the note says what you are playing, and only the arrow says what to do
    // about it.
    //
    // In tune is carried twice over -- the colour changes *and* the arrow goes
    // away -- so it survives a glance, a colourblind reader, and a bad monitor.
    if (!hasSignal)
    {
        return;
    }

    const auto palette = tunerPaletteFor(currentTheme);
    const auto inTune = isNearPerfect();

    // Lighter over a dark background reads as recessed; over a light one it
    // reads as faded. The light palette therefore gets nearly full opacity --
    // the point of the transparency is to let a trace show through, not to make
    // the reading hard to find.
    const auto alpha = isDarkTheme(currentTheme) ? 0.85f : 0.97f;
    const auto noteHeight = juce::jlimit(24.0f, 96.0f, static_cast<float>(area.getHeight()));
    const auto arrowHeight = noteHeight * 0.5f;

    const juce::Font noteFont(juce::FontOptions(noteHeight, juce::Font::bold));
    const juce::Font arrowFont(juce::FontOptions(arrowHeight, juce::Font::bold));

    const auto arrow = inTune            ? juce::String()
                       : zoneCents() > 0 ? juce::String::fromUTF8("\xe2\x96\xb2")
                                         : juce::String::fromUTF8("\xe2\x96\xbc");

    constexpr float gap = 6.0f;
    const auto noteWidth = juce::GlyphArrangement::getStringWidth(noteFont, displayedNote);
    const auto arrowWidth =
        arrow.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth(arrowFont, arrow) + gap;

    // Laid out as one centred group rather than a centred note with something
    // hung off it, so the pair stays centred whichever way the reading goes.
    auto left = static_cast<float>(area.getCentreX()) - (noteWidth + arrowWidth) * 0.5f;
    const auto centreY = static_cast<float>(area.getCentreY());

    graphics.setColour((inTune ? palette.inTune : palette.foreground).withAlpha(alpha));
    graphics.setFont(noteFont);
    graphics.drawText(
        displayedNote,
        juce::Rectangle<float>(left, centreY - noteHeight * 0.5f, noteWidth, noteHeight),
        juce::Justification::centred, false);

    if (arrow.isEmpty())
    {
        return;
    }

    left += noteWidth + gap;

    graphics.setColour(palette.accent.withAlpha(alpha));
    graphics.setFont(arrowFont);
    graphics.drawText(
        arrow, juce::Rectangle<float>(left, centreY - arrowHeight * 0.5f, arrowWidth, arrowHeight),
        juce::Justification::centred, false);
}

void TunerComponent::drawCompactDisplay(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    const auto palette = tunerPaletteFor(currentTheme);

    juce::ignoreUnused(palette);

    // Nothing inside the box without a signal *and* without history -- the
    // graph and the bar both already draw correctly with hasSignal false
    // (drawCompactGraph plots straight from graphHistory; drawCompactBar just
    // skips the marker dot), so this was blanking a pane that had a perfectly
    // good trace to show, every ordinary pause between notes. Blank only
    // belongs to a graph that has nothing in it yet -- the prompt this stood
    // in for before repeating "play or sing" in a 180px pane produced two
    // short wrapped lines that read as a rendering fault. The meter is the
    // exception either way: its whole compact form is a note placeholder,
    // which is legible empty.
    if (!hasSignal && !hasGraphHistory() &&
        static_cast<DisplayMode>(displayModeBox.getSelectedId()) != DisplayMode::meter)
    {
        return;
    }

    switch (static_cast<DisplayMode>(displayModeBox.getSelectedId()))
    {
    case DisplayMode::bar:
        drawCompactBar(graphics, bounds, shape);
        break;

    case DisplayMode::meter:
        drawCompactMeter(graphics, bounds, shape);
        break;

    case DisplayMode::staff:
        // A staff draws at any size: its spacing follows the room.
        drawPitchStaff(graphics, bounds);
        break;

    case DisplayMode::graph:
    default:
        drawCompactGraph(graphics, bounds, shape);
        break;
    }
}

void TunerComponent::drawSelectedDisplay(juce::Graphics& graphics, juce::Rectangle<int> bounds)
    const
{
    // The panel, the watermark, and the border are common to all three modes,
    // so they live here rather than being repeated in each -- and the ordering
    // is the point: the watermark goes down after the background and before the
    // mode draws, so the graph line, the bar, and the meter all sit on top of
    // it. The border goes last so no content can bleed over the rounded edge.
    const auto palette = tunerPaletteFor(currentTheme);

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);

    // Too small for any of the three displays at full size, so each draws its
    // compact form instead -- along whichever axis has room. The border still
    // goes down below, because a compact display is the display, not a message
    // shown in place of one.
    const auto shape = compactShape(bounds.getWidth(), bounds.getHeight());

    if (shape != compact::Shape::full)
    {
        drawCompactDisplay(graphics, bounds, shape);
        graphics.setColour(palette.outline);
        graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

        return;
    }

    const auto mode = static_cast<DisplayMode>(displayModeBox.getSelectedId());

    // Where the note goes depends on what the mode leaves free. The graph and
    // the meter both keep their middle clear, so the note sits behind them,
    // centred, which is where it reads best. The bar lies straight across the
    // middle -- centred there, the track and its "0" tick slice the glyphs in
    // half -- so above the bar it is, in the band the bar does not use.
    // The staff names its note itself, by where it sits.
    if (mode != DisplayMode::staff)
    {
        drawNoteWatermark(graphics, noteArea(bounds));
    }

    switch (mode)
    {
    case DisplayMode::bar:
        drawPitchBar(graphics, bounds);
        break;

    case DisplayMode::staff:
        drawPitchStaff(graphics, bounds);
        break;

    case DisplayMode::meter:
        drawPitchMeter(graphics, bounds);
        break;

    case DisplayMode::graph:
    default:
        drawPitchGraph(graphics, bounds);
        break;
    }

    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);
}

void TunerComponent::drawMarker(
    juce::Graphics& graphics,
    juce::Point<float> at,
    juce::Rectangle<float> span,
    bool vertical) const
{
    if (!hasSignal || marker == Marker::off)
    {
        return;
    }

    // In the colour of the zone the pitch is in, ringed twice so it stands out
    // from a lit band of the same colour (#372): a halo in the background's
    // colour, then an outline in the foreground's. One of the two contrasts
    // with every zone colour in either theme (ThemeContrastTests).
    const auto edges = tunerMarkerEdges(currentTheme);
    const auto fill = intonationZoneColour(pitch::zoneOf(zoneCents(), zoneLimits()));
    constexpr float haloWidth = 7.0f;
    constexpr float outlineWidth = 2.5f;

    // A filled shape: halo, fill, outline.
    const auto ringed = [&graphics, &edges, fill](const juce::Path& shape)
    {
        graphics.setColour(edges.halo);
        graphics.strokePath(shape, juce::PathStrokeType(haloWidth, juce::PathStrokeType::curved));
        graphics.setColour(fill);
        graphics.fillPath(shape);
        graphics.setColour(edges.outline);
        graphics.strokePath(
            shape, juce::PathStrokeType(outlineWidth, juce::PathStrokeType::curved));
    };
    // A stroke: halo, outline, then the zone's colour down its middle.
    const auto stroked = [&graphics, &edges, fill](const juce::Line<float>& line, float width)
    {
        graphics.setColour(edges.halo);
        graphics.drawLine(line, width + 5.0f);
        graphics.setColour(edges.outline);
        graphics.drawLine(line, width + 2.0f);
        graphics.setColour(fill);
        graphics.drawLine(line, width);
    };

    switch (marker)
    {
    case Marker::dot:
    {
        juce::Path dot;
        dot.addEllipse(juce::Rectangle<float>(20.0f, 20.0f).withCentre(at));
        ringed(dot);
        break;
    }
    case Marker::arrow:
    {
        // Its tip on the point, pointing the way the pitch is read along: down
        // onto a bar or a horizontal graph's end, across onto a vertical one.
        juce::Path arrow;
        constexpr float length = 20.0f;
        constexpr float halfWidth = 10.0f;
        if (vertical)
        {
            arrow.addTriangle(
                at, {at.x - halfWidth, at.y - length}, {at.x + halfWidth, at.y - length});
        }
        else
        {
            arrow.addTriangle(
                at, {at.x - length, at.y - halfWidth}, {at.x - length, at.y + halfWidth});
        }
        ringed(arrow);
        break;
    }
    case Marker::line:
    {
        stroked(
            vertical ? juce::Line<float>(at.x, span.getY(), at.x, span.getBottom())
                     : juce::Line<float>(span.getX(), at.y, span.getRight(), at.y),
            3.0f);
        break;
    }
    case Marker::ring:
    {
        // Hollow, so what is under it -- the trace, the zero tick -- shows.
        const auto ring = juce::Rectangle<float>(20.0f, 20.0f).withCentre(at);
        graphics.setColour(edges.halo);
        graphics.drawEllipse(ring, 8.0f);
        graphics.setColour(edges.outline);
        graphics.drawEllipse(ring, 5.0f);
        graphics.setColour(fill);
        graphics.drawEllipse(ring, 3.0f);
        break;
    }
    case Marker::diamond:
    {
        juce::Path diamond;
        constexpr float reach = 11.0f;
        diamond.startNewSubPath(at.x, at.y - reach);
        diamond.lineTo(at.x + reach, at.y);
        diamond.lineTo(at.x, at.y + reach);
        diamond.lineTo(at.x - reach, at.y);
        diamond.closeSubPath();
        ringed(diamond);
        break;
    }
    case Marker::crosshair:
    {
        // A short cross with a gap at its centre, so the point itself stays
        // visible, like a sight.
        constexpr float reach = 13.0f;
        constexpr float gap = 4.0f;
        for (const auto& arm :
             {juce::Line<float>(at.x - reach, at.y, at.x - gap, at.y),
              juce::Line<float>(at.x + gap, at.y, at.x + reach, at.y),
              juce::Line<float>(at.x, at.y - reach, at.x, at.y - gap),
              juce::Line<float>(at.x, at.y + gap, at.x, at.y + reach)})
        {
            stroked(arm, 3.0f);
        }
        break;
    }
    case Marker::off:
        break;
    }
}

juce::Rectangle<int> TunerComponent::noteArea(juce::Rectangle<int> display) const
{
    return static_cast<DisplayMode>(displayModeBox.getSelectedId()) == DisplayMode::bar
               ? display.withTrimmedBottom(display.getHeight() / 2 + 26)
               : display;
}

float TunerComponent::signHeight(juce::Rectangle<int> display) const
{
    return 0.5f * noteFontHeight(noteArea(display));
}

void TunerComponent::paint(juce::Graphics& graphics)
{
    const auto palette = tunerPaletteFor(currentTheme);
    graphics.fillAll(palette.background);

    auto bounds = getLocalBounds().reduced(18);

    // One quiet line of readout, then the display takes everything left. The
    // frequency and cents are a detail you consult; the note is the thing you
    // read at a glance, and it is drawn behind the display instead of above it.
    // The target first: what you are aiming at matters more than the hertz.
    graphics.setColour(onTarget() ? palette.inTune : palette.foreground);
    graphics.setFont(textFont(appearance::TextRole::label, juce::Font::bold));
    graphics.drawText(
        targetText(), bounds.removeFromTop(scaledHeight(targetHeight)),
        juce::Justification::centred, true);

    graphics.setColour(palette.muted);
    graphics.setFont(textFont(appearance::TextRole::body));
    graphics.drawText(
        statusText(), bounds.removeFromTop(scaledHeight(statusHeight)),
        juce::Justification::centred);
    bounds.removeFromTop(statusGap);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    displayBounds = bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));

    drawSelectedDisplay(graphics, displayBounds);
}

void TunerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);

    // Must consume exactly what paint() consumes above the display, or the
    // controls drift away from the box they belong under.
    bounds.removeFromTop(scaledHeight(targetHeight));
    bounds.removeFromTop(scaledHeight(statusHeight));
    bounds.removeFromTop(statusGap);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));

    // Only when nobody adopted it. A docked panel puts the chooser on its
    // header line, and reserving a row here as well would leave a gap where it
    // used to be.
    if (modeChooser != nullptr && !isModeChooserAdopted())
    {
        bounds.removeFromTop(8);
        placeModeChooser(bounds.removeFromTop(scaledHeight(modeChooserHeight)));
    }

    if (!areAdvancedSettingsExpanded)
    {
        return;
    }

    bounds.removeFromTop(8);

    // Beside its slider while there is room for both, above it once there is
    // not. See scaledHeight(stackedAdvancedRowsBelowWidth) for why the inline form stops
    // working -- the slider is left with a track too short to drag and a value
    // box too narrow to read.
    const auto stacked = advancedRowsStack();
    const auto placeSliderRow = [this, &bounds, stacked](juce::Label& label, juce::Slider& slider)
    {
        if (stacked)
        {
            auto row = bounds.removeFromTop(scaledHeight(stackedAdvancedRowHeight));
            label.setBounds(row.removeFromTop(scaledHeight(stackedAdvancedLabelHeight)));
            slider.setBounds(row);

            return;
        }

        auto row = bounds.removeFromTop(scaledHeight(advancedRowHeight));
        label.setBounds(row.removeFromLeft(scaledHeight(120)));
        slider.setBounds(row);
    };

    placeSliderRow(easingLabel, easingSlider);
    placeSliderRow(averagingLabel, averagingSlider);
    placeSliderRow(thresholdLabel, thresholdSlider);
    placeSliderRow(dropoutLabel, dropoutSlider);
    placeSliderRow(durationLabel, durationSlider);
    placeSliderRow(scaleLabel, scaleSlider);

    bounds.removeFromTop(8);

    // Full width when stacked: a 120px button hugging the right edge of a
    // 200px pane reads as something that did not fit rather than as a choice.
    auto buttonRow = bounds.removeFromTop(scaledHeight(36));
    clearGraphButton.setBounds(stacked ? buttonRow : buttonRow.removeFromRight(scaledHeight(120)));
}

bool TunerComponent::showsNoteRow(int midiNote) const
{
    if (!inKeyOnly || pitch::inKey(midiNote, target.keyFifths))
    {
        return true;
    }
    const auto aimingAt = target.target();
    return (hasSignal && midiNote == lockedMidiNote) ||
           (aimingAt != nullptr && aimingAt->midiNoteNumber == midiNote);
}

void TunerComponent::drawPitchStaff(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);
    const auto aimingAt = target.target();

    // The clef the note needs: treble from middle C up, bass below, as the
    // Note View chooses it.
    const int reference =
        hasSignal ? lockedMidiNote : (aimingAt != nullptr ? aimingAt->midiNoteNumber : 69);
    const pitch::StaffClef clef =
        reference >= 60 ? pitch::StaffClef::treble : pitch::StaffClef::bass;

    // A wide staff reads no better than a fitted one: no wider than this.
    const auto area = bounds.reduced(16).toFloat();
    const auto staffArea =
        area.withSizeKeepingCentre(std::min(area.getWidth(), 560.0f), area.getHeight());

    MiniStaffStyle style;
    style.lines = palette.foreground.withAlpha(0.75f);
    style.font = &musicFont.get();
    style.keyFifths = target.keyFifths;
    style.minSpacing = 5.0f;
    style.maxSpacing = 18.0f;
    style.ledgerReach = 1.2f;

    // The target hollow on the left, the note sung filled on the right in the
    // colour of how close it is, as the Note View draws them.
    const float left = staffArea.getX() + miniStaffClefWidth(staffArea, style);
    const float width = std::max(0.0f, staffArea.getRight() - 4.0f - left);
    std::vector<MiniStaffNote> heads;
    if (aimingAt != nullptr)
    {
        if (const auto spelled = pitch::parseNoteName(aimingAt->name))
        {
            heads.push_back({*spelled, left + (width * 0.35f), palette.accent, false});
        }
    }
    std::optional<juce::Colour> sungColour;
    if (hasSignal)
    {
        if (const auto spelled = l10n::spellHeard(lockedMidiNote, target.keyFifths))
        {
            sungColour = intonationZoneColour(pitch::zoneOf(zoneCents(), zoneLimits()));
            heads.push_back({*spelled, left + (width * 0.7f), *sungColour, true});
        }
    }
    drawMiniStaff(graphics, staffArea, clef, heads, style);

    // How far off, under the note sung.
    if (sungColour.has_value())
    {
        const long cents = std::lround(zoneCents());
        const juce::String text =
            (cents > 0 ? "+" : "") + juce::String(cents) + juce::String::fromUTF8(" \xC2\xA2");
        graphics.setColour(*sungColour);
        graphics.setFont(textFont(appearance::TextRole::subheading, juce::Font::bold));
        // Below the staff and the ledger lines a sung note usually needs.
        const float spacing = miniStaffSpacing(staffArea, style);
        graphics.drawText(
            text,
            juce::Rectangle<float>(
                left + (width * 0.7f) - 60.0f, staffArea.getCentreY() + (spacing * 5.0f), 120.0f,
                28.0f),
            juce::Justification::centred);
    }
}
