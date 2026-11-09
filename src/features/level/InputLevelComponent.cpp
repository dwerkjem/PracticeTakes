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
    setOrientation(input_level::Orientation::automatic);
}

std::optional<ToolSettingsPayload> InputLevelComponent::captureSettings() const
{
    return input_level::encode(orientation);
}

void InputLevelComponent::applySettings(const ToolSettingsPayload& payload)
{
    setOrientation(input_level::decode(payload));
}

std::vector<ToolComponent::MenuEntry> InputLevelComponent::optionsMenuEntries()
{
    using input_level::Orientation;
    MenuEntry menu{"Orientation", {}, false, true, {}};
    const auto choice = [this](const juce::String& label, Orientation value)
    {
        return MenuEntry{
            label, [this, value] { setOrientation(value); }, orientation == value, true, {}};
    };
    menu.submenu = {
        choice("Automatic", Orientation::automatic), choice("Horizontal", Orientation::horizontal),
        choice("Vertical", Orientation::vertical)};
    return {menu};
}

bool InputLevelComponent::showView(const juce::String& view)
{
    if (view == "horizontal")
    {
        setOrientation(input_level::Orientation::horizontal);
        return true;
    }
    if (view == "vertical")
    {
        setOrientation(input_level::Orientation::vertical);
        return true;
    }
    return false;
}

void InputLevelComponent::setOrientation(input_level::Orientation choice)
{
    orientation = choice;
    resized();
    repaint();
}

void InputLevelComponent::resized()
{
    auto bounds = getLocalBounds();
    const auto shape = compactShape(bounds.getWidth(), bounds.getHeight());
    compactLayout = shape != compact::Shape::full;
    const bool vertical =
        input_level::drawsVertically(orientation, shape == compact::Shape::vertical);
    meter.setVertical(vertical);

    if (!compactLayout && !vertical)
    {
        bounds = bounds.reduced(22, 18);
        levelArea = bounds.removeFromTop(scaledHeight(46));
        statusArea = bounds.removeFromTop(scaledHeight(26));
        bounds.removeFromTop(scaledHeight(14));
        meter.setShowsScale(true);
        meter.setBounds(bounds.removeFromTop(std::min(
            scaledHeight(76), std::max(scaledHeight(40), bounds.getHeight() - scaledHeight(52)))));
        bounds.removeFromTop(scaledHeight(10));
        detailArea = bounds.removeFromTop(scaledHeight(42));
    }
    else if (!compactLayout)
    {
        // Upright with room to spare: the bar down the left, the words beside it.
        bounds = bounds.reduced(22, 18);
        meter.setShowsScale(true);
        meter.setBounds(bounds.removeFromLeft(scaledHeight(96)));
        bounds.removeFromLeft(scaledHeight(24));
        levelArea = bounds.removeFromTop(scaledHeight(46));
        statusArea = bounds.removeFromTop(scaledHeight(44));
        bounds.removeFromTop(scaledHeight(14));
        detailArea = bounds.removeFromTop(scaledHeight(64));
    }
    else if (vertical)
    {
        bounds = bounds.reduced(8);
        levelArea = bounds.removeFromTop(scaledHeight(28));
        statusArea = {};
        detailArea = {};
        meter.setShowsScale(bounds.getWidth() >= scaledHeight(64));
        meter.setBounds(bounds.withSizeKeepingCentre(
            std::min(bounds.getWidth(), scaledHeight(90)), bounds.getHeight()));
    }
    else
    {
        bounds = bounds.reduced(8);
        // The figure above the bar when there is height for both, so the bar
        // keeps the pane's whole width; beside it in a strip.
        levelArea = bounds.getHeight() >= scaledHeight(80)
                        ? bounds.removeFromTop(scaledHeight(26))
                        : bounds.removeFromLeft(std::min(scaledHeight(110), bounds.getWidth() / 3));
        statusArea = {};
        detailArea = {};
        meter.setShowsScale(bounds.getHeight() >= scaledHeight(44));
        meter.setBounds(bounds.withSizeKeepingCentre(
            bounds.getWidth(), std::min(bounds.getHeight(), scaledHeight(64))));
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
    graphics.setFont(textFont(
        compactLayout ? appearance::TextRole::subheading : appearance::TextRole::display,
        juce::Font::bold));
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
    graphics.setFont(textFont(appearance::TextRole::label));
    if (!reason.isEmpty())
    {
        graphics.setColour(palette.muted);
        graphics.drawFittedText(reason, statusArea, juce::Justification::centredLeft, 2);
    }
    else
    {
        graphics.setColour(counts ? palette.inTune : palette.muted);
        graphics.drawFittedText(
            counts ? "Loud enough to count as playing or singing"
                   : "Too quiet to count: a take ignores sound under the line",
            statusArea, juce::Justification::centredLeft, 2);
    }

    // The peak and the threshold, and where to change it.
    graphics.setColour(palette.muted);
    graphics.setFont(textFont(appearance::TextRole::body));
    auto detail = detailArea;
    const juce::String peak =
        "Peak " + (reason.isEmpty() ? figure(reading.peakDb) : juce::String("not measured"));
    const juce::String below = "Ignore sound below " + figure(limit);
    // One line each where the words are beside an upright bar and narrow.
    if (detail.getHeight() >= scaledHeight(60))
    {
        graphics.drawFittedText(
            peak, detail.removeFromTop(scaledHeight(20)), juce::Justification::centredLeft, 1);
        graphics.drawFittedText(
            below, detail.removeFromTop(scaledHeight(20)), juce::Justification::centredLeft, 1);
    }
    else
    {
        graphics.drawFittedText(
            peak + "     " + below, detail.removeFromTop(scaledHeight(20)),
            juce::Justification::centredLeft, 1);
    }
    graphics.drawFittedText(
        "Set the line in Settings, Practice.", detail, juce::Justification::centredLeft, 1);
}
