#pragma once

#include "../../MainComponent.h"
#include "ToolDragHandle.h"
#include "ToolOptionsButton.h"

#include <functional>
#include <utility>

class MainComponent::ToolWindow final : public juce::DocumentWindow
{
  public:
    ToolWindow(
        const juce::String& title,
        juce::Component& content,
        juce::Point<int> preferredSize,
        std::function<void(juce::Component&)> dragHandler,
        std::function<void()> dockHandler,
        std::function<void()> feedbackHandler,
        std::function<void()> closeHandler)
        : DocumentWindow(title, juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          contentComponent(&content), onDrag(dragHandler), dragHandle(std::move(dragHandler)),
          optionsButton(
              "Dock in workspace",
              std::move(dockHandler),
              std::move(feedbackHandler),
              closeHandler),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&content, true);
        content.addMouseListener(this, false);
        juce::Component::addAndMakeVisible(dragHandle);
        juce::Component::addAndMakeVisible(optionsButton);
        setResizable(true, true);
        setResizeLimits(520, 420, 2400, 1600);
        centreWithSize(preferredSize.x, preferredSize.y);
        setVisible(true);
    }

    ~ToolWindow() override
    {
        releaseContent();
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

    void releaseContent()
    {
        if (contentComponent != nullptr)
        {
            contentComponent->removeMouseListener(this);
            contentComponent = nullptr;
        }
        clearContentComponent();
    }

    void applyAppearance(juce::LookAndFeel* appearance, juce::Colour background)
    {
        setLookAndFeel(appearance);
        setBackgroundColour(background);
        sendLookAndFeelChange();
        repaint();
    }

    void resized() override
    {
        DocumentWindow::resized();
        auto handleBounds = getLocalBounds().reduced(8);
        auto toolBar = handleBounds.removeFromTop(28);
        dragHandle.setBounds(toolBar.removeFromLeft(60));
        optionsButton.setBounds(toolBar.removeFromRight(38));
        dragHandle.toFront(false);
        optionsButton.toFront(false);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStarted = false;
        dragArmed = canStartDrag(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragArmed && !dragStarted && event.getDistanceFromDragStart() >= 5)
        {
            dragStarted = true;
            if (onDrag && contentComponent != nullptr)
            {
                onDrag(*contentComponent);
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
        if (contentComponent == nullptr || event.originalComponent != contentComponent)
        {
            return false;
        }

        constexpr int innerPadding = 14;
        const auto point = event.getEventRelativeTo(contentComponent).getPosition();
        return !contentComponent->getLocalBounds().reduced(innerPadding).contains(point);
    }

    juce::Component* contentComponent;
    std::function<void(juce::Component&)> onDrag;
    ToolDragHandle dragHandle;
    ToolOptionsButton optionsButton;
    std::function<void()> onClose;
    bool dragStarted = false;
    bool dragArmed = false;
};
