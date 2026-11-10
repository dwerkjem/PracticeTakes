#include "ZoneLimitsSlider.h"

#include "../theme/AppLookAndFeel.h"
#include "../theme/Theme.h"

#include <algorithm>
#include <utility>

namespace
{
// The bar's thickness and a handle's size, before the text size is applied.
constexpr float trackThickness = 10.0f;
constexpr float handleSize = 16.0f;
// Room either side, so a handle at either end is drawn whole.
constexpr float sideRoom = 10.0f;
} // namespace

ZoneLimitsSlider::ZoneLimitsSlider(
    zone_limits::Model zoneModel,
    std::function<juce::String(double)> format)
    : model(zoneModel), formatValue(std::move(format))
{
    for (std::size_t index = 0; index < current.size(); ++index)
    {
        current[index] = model.limits[index].minimum;
    }
    setWantsKeyboardFocus(true);
}

void ZoneLimitsSlider::setValues(std::array<double, 3> values)
{
    current = values;
    repaint();
}

void ZoneLimitsSlider::setInk(juce::Colour foreground, juce::Colour background)
{
    ink = foreground;
    paper = background;
    repaint();
}

int ZoneLimitsSlider::preferredHeight() const
{
    // The values above the bar, the bar, and room under it.
    const auto text = appearance::fontFor(*this, appearance::TextRole::small).getHeight();
    return static_cast<int>(std::ceil(text + 6.0f + handleSize + 6.0f));
}

juce::Rectangle<float> ZoneLimitsSlider::track() const
{
    const auto bounds = getLocalBounds().toFloat().reduced(sideRoom, 0.0f);
    const float bottom = bounds.getBottom() - 4.0f - (handleSize * 0.5f);
    return {bounds.getX(), bottom - (trackThickness * 0.5f), bounds.getWidth(), trackThickness};
}

float ZoneLimitsSlider::xOf(double value) const
{
    const auto bar = track();
    const double fraction = std::clamp(value / model.axisMaximum(), 0.0, 1.0);
    return bar.getX() + static_cast<float>(fraction) * bar.getWidth();
}

double ZoneLimitsSlider::valueAt(float x) const
{
    const auto bar = track();
    if (bar.getWidth() <= 0.0f)
    {
        return 0.0;
    }
    return static_cast<double>((x - bar.getX()) / bar.getWidth()) * model.axisMaximum();
}

void ZoneLimitsSlider::paint(juce::Graphics& graphics)
{
    const auto bar = track();

    // The four zones, each as far as its limit: the colours a note is shown
    // in, so the bar reads as the result would.
    const std::array<pitch::IntonationZone, 4> zones{
        pitch::IntonationZone::nearPerfect, pitch::IntonationZone::acceptable,
        pitch::IntonationZone::nearButOff, pitch::IntonationZone::outside};
    juce::Path shape;
    shape.addRoundedRectangle(bar, trackThickness * 0.5f);
    graphics.saveState();
    graphics.reduceClipRegion(shape);
    float from = bar.getX();
    for (std::size_t index = 0; index < zones.size(); ++index)
    {
        const float to = index < current.size() ? xOf(current[index]) : bar.getRight();
        graphics.setColour(intonationZoneColour(zones[index]));
        graphics.fillRect(
            juce::Rectangle<float>(from, bar.getY(), std::max(0.0f, to - from), bar.getHeight()));
        from = to;
    }
    graphics.restoreState();

    // Each limit: a handle on the bar, its value over it. Values that would
    // overlap are pushed apart, innermost leftwards.
    const auto font = appearance::fontFor(*this, appearance::TextRole::small);
    graphics.setFont(font);
    std::array<float, 3> centres{};
    std::array<float, 3> widths{};
    for (std::size_t index = 0; index < current.size(); ++index)
    {
        widths[index] =
            juce::GlyphArrangement::getStringWidth(font, formatValue(current[index])) + 6.0f;
        centres[index] = xOf(current[index]);
    }
    for (std::size_t index = 1; index < centres.size(); ++index)
    {
        const float least = centres[index - 1] + ((widths[index - 1] + widths[index]) * 0.5f);
        centres[index] = std::max(centres[index], least);
    }
    const float right = static_cast<float>(getWidth());
    for (std::size_t index = centres.size(); index-- > 0;)
    {
        const float most = index + 1 < centres.size()
                               ? centres[index + 1] - ((widths[index] + widths[index + 1]) * 0.5f)
                               : right - (widths[index] * 0.5f);
        centres[index] = std::max(widths[index] * 0.5f, std::min(centres[index], most));
    }

    for (std::size_t index = 0; index < current.size(); ++index)
    {
        const float x = xOf(current[index]);
        const juce::Rectangle<float> handle(
            x - (handleSize * 0.5f), bar.getCentreY() - (handleSize * 0.5f), handleSize,
            handleSize);
        graphics.setColour(paper);
        graphics.fillEllipse(handle);
        graphics.setColour(
            dragging && index == held ? ink : intonationZoneColour(zones[index]).darker(0.2f));
        graphics.drawEllipse(handle.reduced(1.0f), 2.5f);

        graphics.setColour(ink);
        graphics.drawText(
            formatValue(current[index]),
            juce::Rectangle<float>(
                centres[index] - (widths[index] * 0.5f), 0.0f, widths[index],
                bar.getY() - (handleSize * 0.5f) + (trackThickness * 0.5f) - 2.0f),
            juce::Justification::centredBottom, false);
    }

    if (hasKeyboardFocus(false))
    {
        graphics.setColour(ink.withAlpha(0.35f));
        graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.0f);
    }
}

void ZoneLimitsSlider::moveTo(std::size_t index, double value)
{
    const auto next = zone_limits::moved(model, current, index, value);
    if (next == current)
    {
        return;
    }
    current = next;
    repaint();
    if (onChange)
    {
        onChange();
    }
}

void ZoneLimitsSlider::mouseDown(const juce::MouseEvent& event)
{
    held = zone_limits::nearest(current, valueAt(event.position.x));
    dragging = true;
    grabKeyboardFocus();
    moveTo(held, valueAt(event.position.x));
    repaint();
}

void ZoneLimitsSlider::mouseDrag(const juce::MouseEvent& event)
{
    moveTo(held, valueAt(event.position.x));
}

void ZoneLimitsSlider::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

bool ZoneLimitsSlider::keyPressed(const juce::KeyPress& key)
{
    // Tab and shift-tab choose the handle; the arrows move it a step.
    if (key == juce::KeyPress::tabKey && held + 1 < current.size())
    {
        ++held;
        repaint();
        return true;
    }
    if (key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0) &&
        held > 0)
    {
        --held;
        repaint();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        moveTo(held, current[held] - model.step);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::rightKey || key.getKeyCode() == juce::KeyPress::upKey)
    {
        moveTo(held, current[held] + model.step);
        return true;
    }
    return false;
}
