#pragma once

#include "../../../MainComponent.h"
#include "ToolOptionsButton.h"

#include <functional>
#include <utility>

class MainComponent::DockedToolPanel final : public juce::Component
{
  public:
    DockedToolPanel(
        const juce::String& title,
        juce::Component& toolContent,
        std::function<void(juce::Component&)> dragHandler,
        std::function<void()> floatHandler,
        std::function<void()> feedbackHandler,
        std::function<void()> closeHandler,
        std::function<void()> focusHandler)
        : content(&toolContent), onDrag(std::move(dragHandler)), onFocus(std::move(focusHandler)),
          optionsButton(
              "Float in window",
              std::move(floatHandler),
              std::move(feedbackHandler),
              std::move(closeHandler))
    {
        titleLabel.setText(title, juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(titleLabel);
        addAndMakeVisible(optionsButton);

        addAndMakeVisible(toolContent);
        toolContent.addMouseListener(this, true);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    ~DockedToolPanel() override
    {
        releaseContent();
    }

    void releaseContent()
    {
        if (content != nullptr)
        {
            content->removeMouseListener(this);
            removeChildComponent(content);
            content = nullptr;
        }
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto background = findColour(juce::ResizableWindow::backgroundColourId);
        graphics.fillAll(background);
        graphics.setColour(findColour(juce::ComboBox::outlineColourId));
        graphics.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);
        auto header = bounds.removeFromTop(32);
        optionsButton.setBounds(header.removeFromRight(38));
        header.removeFromRight(4);
        titleLabel.setBounds(header);
        bounds.removeFromTop(4);
        if (content != nullptr)
        {
            content->setBounds(bounds);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (onFocus)
        {
            onFocus();
        }
        dragStarted = false;
        dragArmed = canStartDrag(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragArmed && !dragStarted && event.getDistanceFromDragStart() >= 5)
        {
            dragStarted = true;
            if (onDrag)
            {
                onDrag(*this);
            }
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragStarted = false;
        dragArmed = false;
    }

  private:
    [[nodiscard]] bool canStartDrag(const juce::MouseEvent& event) const
    {
        if (event.originalComponent == this)
        {
            return true;
        }
        if (content == nullptr || event.originalComponent != content)
        {
            return false;
        }

        constexpr int innerPadding = 14;
        const auto point = event.getEventRelativeTo(content).getPosition();
        return !content->getLocalBounds().reduced(innerPadding).contains(point);
    }

    juce::Component* content;
    juce::Label titleLabel;
    std::function<void(juce::Component&)> onDrag;
    std::function<void()> onFocus;
    ToolOptionsButton optionsButton;
    bool dragStarted = false;
    bool dragArmed = false;
};
