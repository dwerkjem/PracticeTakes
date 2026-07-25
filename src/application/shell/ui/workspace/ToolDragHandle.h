#pragma once

#include <JuceHeader.h>

#include <functional>
#include <utility>

class ToolDragHandle final : public juce::Component
{
  public:
    explicit ToolDragHandle(std::function<void(juce::Component&)> dragHandler)
        : onDrag(std::move(dragHandler))
    {
        setTitle("Drag tool");
        setDescription("Drag this tool to a workspace drop target");
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().toFloat();
        graphics.setColour(findColour(juce::TextButton::buttonColourId));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(findColour(juce::TextButton::textColourOffId));
        graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        graphics.drawText("Drag", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragStarted = false;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragStarted && event.getDistanceFromDragStart() >= 5)
        {
            dragStarted = true;
            if (onDrag)
                onDrag(*this);
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragStarted = false;
    }

  private:
    std::function<void(juce::Component&)> onDrag;
    bool dragStarted = false;
};
