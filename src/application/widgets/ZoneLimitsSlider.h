#pragma once

#include <JuceHeader.h>

#include "ZoneLimitsModel.h"

#include <array>
#include <functional>

// Three nested limits on one bar: green to the first, blue to the second,
// amber to the third and red beyond, each limit a handle with its value above
// it. Pitch's limits (near perfect, in tune, out of tune) and timing's (right
// on, on time, off the beat) are each set with one, in Settings -> Practice.
// Where a dragged handle may go is zone_limits::moved.
class ZoneLimitsSlider final : public juce::Component, public juce::SettableTooltipClient
{
  public:
    ZoneLimitsSlider(zone_limits::Model model, std::function<juce::String(double)> format);

    // Shown as they are, without telling `onChange`.
    void setValues(std::array<double, 3> values);
    [[nodiscard]] std::array<double, 3> values() const noexcept
    {
        return current;
    }

    // After the reader moves a handle.
    std::function<void()> onChange;

    // The track's background, behind the colours.
    void setInk(juce::Colour foreground, juce::Colour background);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    // A handle with the keyboard focus moves a step with the arrow keys.
    bool keyPressed(const juce::KeyPress& key) override;

    [[nodiscard]] int preferredHeight() const;

  private:
    [[nodiscard]] juce::Rectangle<float> track() const;
    [[nodiscard]] float xOf(double value) const;
    [[nodiscard]] double valueAt(float x) const;
    void moveTo(std::size_t index, double value);

    zone_limits::Model model;
    std::function<juce::String(double)> formatValue;
    std::array<double, 3> current{};
    std::size_t held = 0;
    bool dragging = false;
    juce::Colour ink = juce::Colours::black;
    juce::Colour paper = juce::Colours::white;
};
