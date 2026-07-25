#pragma once

#include "../MainComponent.h"

#include <functional>
#include <utility>

class MainComponent::DockedToolPanel final : public juce::Component
{
  public:
    DockedToolPanel(
        const juce::String& title,
        juce::Component& toolContent,
        std::function<void()> floatHandler,
        std::function<void()> closeHandler)
        : content(&toolContent)
    {
        titleLabel.setText(title, juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        addAndMakeVisible(titleLabel);

        floatButton.setButtonText("Float");
        floatButton.setTooltip("Move this tool to an independent window");
        floatButton.onClick = std::move(floatHandler);
        addAndMakeVisible(floatButton);

        closeButton.setButtonText("Close");
        closeButton.setTooltip("Close this tool");
        closeButton.onClick = std::move(closeHandler);
        addAndMakeVisible(closeButton);

        addAndMakeVisible(toolContent);
    }

    ~DockedToolPanel() override
    {
        releaseContent();
    }

    void releaseContent()
    {
        if (content != nullptr)
        {
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
        auto bounds = getLocalBounds().reduced(8);
        auto header = bounds.removeFromTop(38);
        closeButton.setBounds(header.removeFromRight(86));
        header.removeFromRight(6);
        floatButton.setBounds(header.removeFromRight(86));
        titleLabel.setBounds(header);
        bounds.removeFromTop(6);
        if (content != nullptr)
            content->setBounds(bounds);
    }

  private:
    juce::Component* content;
    juce::Label titleLabel;
    juce::TextButton floatButton;
    juce::TextButton closeButton;
};
