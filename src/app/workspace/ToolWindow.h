#pragma once

#include "../MainComponent.h"

#include <functional>
#include <utility>

class MainComponent::ToolWindow final : public juce::DocumentWindow
{
  public:
    ToolWindow(
        const juce::String& title,
        juce::Component& content,
        juce::Point<int> preferredSize,
        std::function<void()> closeHandler)
        : DocumentWindow(title, juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&content, true);
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

  private:
    std::function<void()> onClose;
};
