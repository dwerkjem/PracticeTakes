#include "TunerComponent.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double inTuneToleranceCents = 5.0;

constexpr std::array<const char*, 12> noteNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                "F#", "G",  "G#", "A",  "A#", "B"};

[[nodiscard]] juce::String noteNameForMidi(int midiNote)
{
    const auto noteIndex = ((midiNote % 12) + 12) % 12;
    const auto octave = (midiNote / 12) - 1;

    return juce::String(noteNames[static_cast<std::size_t>(noteIndex)]) + juce::String(octave);
}
} // namespace

//==============================================================================
// Drawing

void TunerComponent::drawPitchGraph(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    auto content = bounds.reduced(8);
    auto labelArea = content.removeFromLeft(48);
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
    }

    const auto firstNote = static_cast<int>(std::ceil(minimumValue));
    const auto lastNote = static_cast<int>(std::floor(maximumValue));
    const auto pixelsPerSemitone =
        static_cast<double>(plotArea.getHeight()) / std::max(1.0, maximumValue - minimumValue);

    graphics.setFont(juce::FontOptions(11.0f));

    for (int midiNote = firstNote; midiNote <= lastNote; ++midiNote)
    {
        const auto y =
            juce::jmap(static_cast<float>(midiNote), static_cast<float>(minimumValue),
                       static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
                       static_cast<float>(plotArea.getY()));

        const auto isCurrentNote = hasSignal && midiNote == lockedMidiNote;
        const auto isC = ((midiNote % 12) + 12) % 12 == 0;

        graphics.setColour(isCurrentNote ? palette.accent.withAlpha(0.48f)
                           : isC         ? palette.muted.withAlpha(0.30f)
                                         : palette.outline.withAlpha(0.62f));
        graphics.drawHorizontalLine(static_cast<int>(std::round(y)),
                                    static_cast<float>(plotArea.getX()),
                                    static_cast<float>(plotArea.getRight()));

        if (pixelsPerSemitone >= 10.0 || isCurrentNote || isC)
        {
            graphics.setColour(isCurrentNote ? palette.accent : palette.muted);
            graphics.drawFittedText(
                noteNameForMidi(midiNote),
                labelArea.withY(static_cast<int>(std::round(y)) - 8).withHeight(16),
                juce::Justification::centredRight, 1);
        }
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
        const auto y =
            juce::jmap(static_cast<float>(value), static_cast<float>(minimumValue),
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
}

void TunerComponent::drawPitchBar(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);
    const auto indicatorColour =
        std::abs(displayedCents) <= inTuneToleranceCents ? palette.inTune : palette.accent;

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    auto barBounds = bounds.reduced(28, 34);
    const auto centreY = barBounds.getCentreY();

    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(
        barBounds.toFloat().withHeight(10.0f).withCentre(
            {static_cast<float>(barBounds.getCentreX()), static_cast<float>(centreY)}),
        5.0f);

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : {-50, -25, 0, 25, 50})
    {
        const auto x = juce::jmap(static_cast<float>(cents), -50.0f, 50.0f,
                                  static_cast<float>(barBounds.getX()),
                                  static_cast<float>(barBounds.getRight()));

        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawVerticalLine(static_cast<int>(std::round(x)),
                                  static_cast<float>(centreY) - 18.0f,
                                  static_cast<float>(centreY) + 18.0f);
        graphics.drawText(juce::String(cents > 0 ? "+" : "") + juce::String(cents),
                          static_cast<int>(x) - 24, centreY + 22, 48, 18,
                          juce::Justification::centred);
    }

    if (!hasSignal)
    {
        return;
    }

    const auto indicatorX =
        juce::jmap(static_cast<float>(juce::jlimit(-50.0, 50.0, displayedCents)), -50.0f, 50.0f,
                   static_cast<float>(barBounds.getX()), static_cast<float>(barBounds.getRight()));

    graphics.setColour(indicatorColour);
    graphics.fillEllipse(indicatorX - 10.0f, static_cast<float>(centreY) - 10.0f, 20.0f, 20.0f);
}

void TunerComponent::drawPitchMeter(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);
    const auto indicatorColour =
        std::abs(displayedCents) <= inTuneToleranceCents ? palette.inTune : palette.accent;

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

    const auto centre = juce::Point<float>(static_cast<float>(bounds.getCentreX()),
                                           static_cast<float>(bounds.getBottom() - 24));
    const auto radius = static_cast<float>(
        std::max(30, std::min(bounds.getWidth() / 2 - 28, bounds.getHeight() - 54)));

    constexpr double startAngle = -2.45;
    constexpr double endAngle = -0.69;

    juce::Path arc;
    for (int step = 0; step <= 64; ++step)
    {
        const auto proportion = static_cast<double>(step) / 64.0;
        const auto angle = startAngle + proportion * (endAngle - startAngle);
        const auto point =
            centre + juce::Point<float>(static_cast<float>(std::cos(angle) * radius),
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

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : {-50, -25, 0, 25, 50})
    {
        const auto angle =
            juce::jmap(static_cast<double>(cents), -50.0, 50.0, startAngle, endAngle);
        const auto outerPoint =
            centre + juce::Point<float>(static_cast<float>(std::cos(angle) * radius),
                                        static_cast<float>(std::sin(angle) * radius));
        const auto innerPoint =
            centre + juce::Point<float>(static_cast<float>(std::cos(angle) * (radius - 15.0f)),
                                        static_cast<float>(std::sin(angle) * (radius - 15.0f)));

        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawLine({innerPoint, outerPoint}, cents == 0 ? 2.0f : 1.0f);
    }

    const auto needleCents = hasSignal ? juce::jlimit(-50.0, 50.0, displayedCents) : 0.0;
    const auto needleAngle = juce::jmap(needleCents, -50.0, 50.0, startAngle, endAngle);
    const auto needleEnd =
        centre + juce::Point<float>(static_cast<float>(std::cos(needleAngle) * (radius - 20.0f)),
                                    static_cast<float>(std::sin(needleAngle) * (radius - 20.0f)));

    graphics.setColour(hasSignal ? indicatorColour : palette.muted);
    graphics.drawLine({centre, needleEnd}, 3.0f);
    graphics.fillEllipse(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);

    graphics.setColour(palette.muted);
    graphics.drawText("FLAT", bounds.removeFromLeft(74).removeFromBottom(24),
                      juce::Justification::centred);
    graphics.drawText("SHARP", bounds.removeFromRight(74).removeFromBottom(24),
                      juce::Justification::centred);
}

void TunerComponent::drawSelectedDisplay(juce::Graphics& graphics,
                                         juce::Rectangle<int> bounds) const
{
    switch (static_cast<DisplayMode>(displayModeBox.getSelectedId()))
    {
    case DisplayMode::bar:
        drawPitchBar(graphics, bounds);
        break;

    case DisplayMode::meter:
        drawPitchMeter(graphics, bounds);
        break;

    case DisplayMode::graph:
    default:
        drawPitchGraph(graphics, bounds);
        break;
    }
}

void TunerComponent::paint(juce::Graphics& graphics)
{
    const auto palette = tunerPaletteFor(currentTheme);
    graphics.fillAll(palette.background);

    auto bounds = getLocalBounds().reduced(18);
    auto noteArea = bounds.removeFromTop(142);

    graphics.setColour(hasSignal ? palette.foreground : palette.muted);
    graphics.setFont(juce::FontOptions(78.0f, juce::Font::bold));
    graphics.drawText(displayedNote, noteArea.removeFromTop(96), juce::Justification::centred);

    graphics.setFont(juce::FontOptions(18.0f));
    graphics.drawFittedText(statusText(), noteArea.removeFromTop(42), juce::Justification::centred,
                            2);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    displayBounds = bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));

    drawSelectedDisplay(graphics, displayBounds);
}

void TunerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    feedbackButton.setBounds(bounds.removeFromTop(34).removeFromRight(220));
    bounds = getLocalBounds().reduced(18);
    bounds.removeFromTop(142);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));
    bounds.removeFromTop(8);

    auto displayRow = bounds.removeFromTop(32);
    displayModeLabel.setBounds(displayRow.removeFromLeft(110));
    displayModeBox.setBounds(displayRow);

    bounds.removeFromTop(8);
    advancedSettingsButton.setBounds(bounds.removeFromTop(34));

    if (!areAdvancedSettingsExpanded)
    {
        return;
    }

    bounds.removeFromTop(8);
    const auto placeSliderRow = [&bounds](juce::Label& label, juce::Slider& slider)
    {
        auto row = bounds.removeFromTop(30);
        label.setBounds(row.removeFromLeft(120));
        slider.setBounds(row);
    };

    placeSliderRow(easingLabel, easingSlider);
    placeSliderRow(averagingLabel, averagingSlider);
    placeSliderRow(thresholdLabel, thresholdSlider);
    placeSliderRow(dropoutLabel, dropoutSlider);
    placeSliderRow(durationLabel, durationSlider);

    bounds.removeFromTop(8);
    clearGraphButton.setBounds(bounds.removeFromTop(36).removeFromRight(120));
}
