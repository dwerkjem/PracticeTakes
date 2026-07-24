#pragma once

#include <JuceHeader.h>

#include <functional>

class MainTitleBar final : public juce::Component
{
  public:
    MainTitleBar(
        const juce::String& windowTitle,
        juce::TextButton& file,
        juce::TextButton& settings,
        juce::TextButton& tools,
        juce::TextButton& help,
        juce::TextButton& microphone,
        std::function<void()> minimiseHandler,
        std::function<void()> fullscreenHandler,
        std::function<void()> closeHandler)
        : title(windowTitle), fileButton(file), settingsButton(settings), toolsButton(tools),
          helpButton(help), microphoneButton(microphone), onFullscreen(std::move(fullscreenHandler))
    {
        for (auto* button :
             {&fileButton, &settingsButton, &toolsButton, &helpButton, &microphoneButton})
        {
            addAndMakeVisible(button);
        }

        hamburgerButton.setButtonText("Menu");
        hamburgerButton.setTooltip("Show application menu");
        hamburgerButton.onClick = [this] { showCollapsedMenu(); };
        addChildComponent(hamburgerButton);

        minimiseButton.setButtonText("-");
        minimiseButton.setTooltip("Minimise window");
        minimiseButton.onClick = std::move(minimiseHandler);
        addAndMakeVisible(minimiseButton);

        fullscreenButton.setTooltip("Enter fullscreen (F11)");
        fullscreenButton.onClick = onFullscreen;
        addAndMakeVisible(fullscreenButton);

        closeButton.setButtonText("X");
        closeButton.setTooltip("Close Practice Takes");
        closeButton.onClick = std::move(closeHandler);
        addAndMakeVisible(closeButton);

        setFullscreen(false);
    }

    void setFullscreen(bool isFullscreen)
    {
        fullscreenButton.setButtonText(isFullscreen ? "Restore" : "Fullscreen");
        fullscreenButton.setTooltip(
            isFullscreen ? "Exit fullscreen (F11 or Escape)" : "Enter fullscreen (F11)");
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto background = findColour(juce::ResizableWindow::backgroundColourId);
        graphics.fillAll(background.darker(0.2f));
        graphics.setColour(findColour(juce::Label::textColourId));
        graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        graphics.drawText(title, titleBounds, juce::Justification::centredLeft, true);
        graphics.setColour(findColour(juce::ComboBox::outlineColourId));
        graphics.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6, 5);
        constexpr int menuWidth = 96;
        constexpr int gap = 4;
        constexpr int windowButtonWidth = 76;

        closeButton.setBounds(bounds.removeFromRight(38));
        bounds.removeFromRight(gap);
        fullscreenButton.setBounds(bounds.removeFromRight(windowButtonWidth));
        bounds.removeFromRight(gap);
        minimiseButton.setBounds(bounds.removeFromRight(38));
        bounds.removeFromRight(gap);

        const auto useCollapsedMenu = getWidth() < 900;
        hamburgerButton.setVisible(useCollapsedMenu);
        for (auto* button :
             {&fileButton, &settingsButton, &toolsButton, &helpButton, &microphoneButton})
        {
            button->setVisible(!useCollapsedMenu);
        }

        if (useCollapsedMenu)
        {
            hamburgerButton.setBounds(bounds.removeFromLeft(76));
            bounds.removeFromLeft(12);
        }
        else
        {
            microphoneButton.setBounds(bounds.removeFromRight(190));
            bounds.removeFromRight(12);

            fileButton.setBounds(bounds.removeFromLeft(menuWidth));
            bounds.removeFromLeft(gap);
            settingsButton.setBounds(bounds.removeFromLeft(menuWidth));
            bounds.removeFromLeft(gap);
            toolsButton.setBounds(bounds.removeFromLeft(menuWidth));
            bounds.removeFromLeft(gap);
            helpButton.setBounds(bounds.removeFromLeft(menuWidth));
            bounds.removeFromLeft(12);
        }
        titleBounds = bounds;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        {
            windowDragger.startDraggingComponent(window, event);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>();
            window != nullptr && !window->isFullScreen())
        {
            windowDragger.dragComponent(window, event, nullptr);
        }
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (onFullscreen)
        {
            onFullscreen();
        }
    }

  private:
    void showCollapsedMenu()
    {
        enum MenuItem
        {
            file = 1,
            settings,
            tools,
            help,
            microphone
        };

        juce::PopupMenu menu;
        menu.addItem(file, "File", false);
        menu.addItem(settings, "Settings");
        menu.addItem(tools, "Tools");
        menu.addItem(help, "Help");
        menu.addSeparator();
        menu.addItem(microphone, microphoneButton.getButtonText());

        const auto safeThis = juce::Component::SafePointer<MainTitleBar>(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&hamburgerButton).withMinimumWidth(220),
            [safeThis](int selectedItemId)
            {
                if (safeThis == nullptr)
                {
                    return;
                }

                switch (selectedItemId)
                {
                case file:
                    safeThis->fileButton.triggerClick();
                    break;
                case settings:
                    safeThis->settingsButton.triggerClick();
                    break;
                case tools:
                    safeThis->toolsButton.triggerClick();
                    break;
                case help:
                    safeThis->helpButton.triggerClick();
                    break;
                case microphone:
                    safeThis->microphoneButton.triggerClick();
                    break;
                default:
                    break;
                }
            });
    }

    juce::String title;
    juce::TextButton& fileButton;
    juce::TextButton& settingsButton;
    juce::TextButton& toolsButton;
    juce::TextButton& helpButton;
    juce::TextButton& microphoneButton;
    juce::TextButton hamburgerButton;
    juce::TextButton minimiseButton;
    juce::TextButton fullscreenButton;
    juce::TextButton closeButton;
    juce::Rectangle<int> titleBounds;
    juce::ComponentDragger windowDragger;
    std::function<void()> onFullscreen;
};
