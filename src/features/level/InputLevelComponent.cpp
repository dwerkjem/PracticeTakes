#include "InputLevelComponent.h"

#include "../../application/theme/Theme.h"
#include "../../application/tools/CompactPresentation.h"

#include <cmath>
#include <utility>

namespace
{
[[nodiscard]] juce::String minus()
{
    return juce::String(juce::CharPointer_UTF8("\xe2\x88\x92"));
}

[[nodiscard]] juce::String signedFigure(double db)
{
    const auto rounded = static_cast<int>(std::lround(db));
    return rounded < 0 ? minus() + juce::String(-rounded) : juce::String(rounded);
}
} // namespace

InputLevelComponent::InputLevelComponent(
    AudioInputService& audioInputService,
    std::function<double()> thresholdDb,
    Theme initialTheme)
    : theme(initialTheme),
      meter(
          audioInputService,
          std::move(thresholdDb),
          InputLevelMeter::Listening::always,
          initialTheme)
{
    setOpaque(true);
    meter.setComponentID("input-level-meter");
    meter.setTooltip(
        "The bar is the microphone's level; the mark, its loudest lately. Sound under the line "
        "is not heard as a note: set the line in Settings, Practice, Ignore sound below");
    meter.onUpdate = [this] { repaint(levelArea.getUnion(statusArea).getUnion(detailArea)); };
    addAndMakeVisible(meter);
}

juce::String InputLevelComponent::figure(double db)
{
    if (db < InputLevelMeter::lowestDb)
    {
        return "below " + signedFigure(InputLevelMeter::lowestDb) + " dB";
    }
    return signedFigure(db) + " dB";
}

void InputLevelComponent::setTheme(Theme newTheme)
{
    theme = newTheme;
    meter.setTheme(newTheme);
    repaint();
}

void InputLevelComponent::resetToDefaults()
{
    meter.reset();
}

void InputLevelComponent::resized()
{
    auto bounds = getLocalBounds();
    const auto shape = compact::shapeFor(bounds.getWidth(), bounds.getHeight());
    compactLayout = shape != compact::Shape::full;
    meter.setVertical(shape == compact::Shape::vertical);

    switch (shape)
    {
    case compact::Shape::full:
    {
        bounds = bounds.reduced(22, 18);
        levelArea = bounds.removeFromTop(46);
        statusArea = bounds.removeFromTop(26);
        bounds.removeFromTop(14);
        meter.setShowsScale(true);
        meter.setBounds(bounds.removeFromTop(std::min(76, std::max(40, bounds.getHeight() - 52))));
        bounds.removeFromTop(10);
        detailArea = bounds.removeFromTop(42);
        break;
    }
    case compact::Shape::horizontal:
    {
        bounds = bounds.reduced(8);
        // The figure above the bar when there is height for both, so the bar
        // keeps the pane's whole width; beside it in a strip.
        levelArea = bounds.getHeight() >= 80
                        ? bounds.removeFromTop(26)
                        : bounds.removeFromLeft(std::min(110, bounds.getWidth() / 3));
        statusArea = {};
        detailArea = {};
        meter.setShowsScale(bounds.getHeight() >= 44);
        meter.setBounds(
            bounds.withSizeKeepingCentre(bounds.getWidth(), std::min(bounds.getHeight(), 64)));
        break;
    }
    case compact::Shape::vertical:
    {
        bounds = bounds.reduced(8);
        levelArea = bounds.removeFromTop(28);
        statusArea = {};
        detailArea = {};
        meter.setShowsScale(bounds.getWidth() >= 64);
        meter.setBounds(
            bounds.withSizeKeepingCentre(std::min(bounds.getWidth(), 90), bounds.getHeight()));
        break;
    }
    }
}

void InputLevelComponent::paint(juce::Graphics& graphics)
{
    const auto palette = tunerPaletteFor(theme);
    graphics.fillAll(palette.background);

    const auto reading = meter.reading();
    const auto& reason = meter.unavailableReason();
    const double limit = meter.thresholdDb();
    const bool counts = reason.isEmpty() && reading.reaches(limit);

    // The level, large.
    graphics.setColour(palette.foreground);
    graphics.setFont(juce::FontOptions(compactLayout ? 16.0f : 34.0f, juce::Font::bold));
    graphics.drawFittedText(
        reason.isEmpty() ? figure(reading.levelDb)
                         : juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")),
        levelArea, compactLayout ? juce::Justification::centred : juce::Justification::centredLeft,
        1);

    if (compactLayout)
    {
        return;
    }

    // Which side of the line the sound is, in words.
    graphics.setFont(juce::FontOptions(15.0f));
    if (!reason.isEmpty())
    {
        graphics.setColour(palette.muted);
        graphics.drawFittedText(reason, statusArea, juce::Justification::centredLeft, 1);
    }
    else
    {
        graphics.setColour(counts ? palette.inTune : palette.muted);
        graphics.drawFittedText(
            counts ? "Loud enough to count as playing or singing"
                   : "Too quiet to count: a take ignores sound under the line",
            statusArea, juce::Justification::centredLeft, 1);
    }

    // The peak and the threshold, and where to change it.
    graphics.setColour(palette.muted);
    graphics.setFont(juce::FontOptions(13.0f));
    auto detail = detailArea;
    graphics.drawFittedText(
        "Peak " + (reason.isEmpty() ? figure(reading.peakDb) : juce::String("not measured")) +
            "     Ignore sound below " + figure(limit),
        detail.removeFromTop(20), juce::Justification::centredLeft, 1);
    graphics.drawFittedText(
        "Set the line in Settings, Practice.", detail, juce::Justification::centredLeft, 1);
}
