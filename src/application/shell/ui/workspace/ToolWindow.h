#pragma once

#include "../../MainComponent.h"
#include "ToolDragHandle.h"

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
        std::function<void()> closeHandler)
        : DocumentWindow(title, juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          dragHandle(std::move(dragHandler)), onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&content, true);
        juce::Component::addAndMakeVisible(dragHandle);
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
        dragHandle.setBounds(handleBounds.removeFromTop(28).removeFromLeft(72));
        dragHandle.toFront(false);
    }

  private:
    ToolDragHandle dragHandle;
    std::function<void()> onClose;
};
