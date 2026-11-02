#pragma once

#include <JuceHeader.h>

#include <functional>

// The application's own control strip, along the top of the window's content:
// the menu buttons (or the collapsed Menu below 900 px), the microphone, and
// fullscreen.
//
// It used to be the window's title bar as well, drawing the title and its own
// minimise and close buttons and dragging the window. The window now uses the
// operating system's title bar for all of that, so this is only the strip of
// controls the operating system has no equivalent for. The name stays, because
// test-control code and comments name it.
class MainTitleBar final : public juce::Component
{
  public:
    MainTitleBar(
        juce::TextButton& file,
        juce::TextButton& settings,
        juce::TextButton& tools,
        juce::TextButton& help,
        juce::TextButton& microphone,
        std::function<void()> fullscreenHandler)
        : fileButton(file), settingsButton(settings), toolsButton(tools), helpButton(help),
          microphoneButton(microphone), onFullscreen(std::move(fullscreenHandler))
    {
        for (auto* button :
             {&fileButton, &settingsButton, &toolsButton, &helpButton, &microphoneButton})
        {
            addAndMakeVisible(button);
        }

        // Component ids let the development-only test control channel address
        // these by name rather than by screen position. The names must match
        // the approved click targets in
        // src/application/testcontrol/ApprovedWindowStates.cpp.
        //
        // The hamburger is only visible below the collapsed-menu width, so a
        // click on it is legitimately unavailable in a wide window -- which the
        // channel reports as a failure rather than a silent no-op.
        hamburgerButton.setButtonText("Menu");
        hamburgerButton.setTooltip("Show application menu");
        hamburgerButton.setComponentID("hamburger-button");
        hamburgerButton.onClick = [this] { showCollapsedMenu(); };
        addChildComponent(hamburgerButton);

        fullscreenButton.setTooltip("Enter fullscreen (F11)");
        fullscreenButton.setComponentID("fullscreen-button");
        fullscreenButton.onClick = onFullscreen;
        addAndMakeVisible(fullscreenButton);

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
        graphics.setColour(findColour(juce::ComboBox::outlineColourId));
        graphics.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6, 5);
        constexpr int menuWidth = 96;
        constexpr int gap = 4;
        constexpr int windowButtonWidth = 76;

        fullscreenButton.setBounds(bounds.removeFromRight(windowButtonWidth));
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

    juce::TextButton& fileButton;
    juce::TextButton& settingsButton;
    juce::TextButton& toolsButton;
    juce::TextButton& helpButton;
    juce::TextButton& microphoneButton;
    juce::TextButton hamburgerButton;
    juce::TextButton fullscreenButton;
    std::function<void()> onFullscreen;
};
