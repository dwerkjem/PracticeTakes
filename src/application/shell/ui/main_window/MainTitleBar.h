#pragma once

#include "../../../theme/AppLookAndFeel.h"

#include "../../../localisation/Localisation.h"
#include <JuceHeader.h>

#include "TransportStrip.h"

#include <algorithm>
#include <functional>

// The application's own control strip, along the top of the window's content:
// the menu buttons (or the collapsed Menu below 900 px), the practice clock's
// transport once a score is open, the microphone, and fullscreen.
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
        TransportStrip& transport,
        std::function<void()> fullscreenHandler)
        : fileButton(file), settingsButton(settings), toolsButton(tools), helpButton(help),
          microphoneButton(microphone), transportStrip(transport),
          onFullscreen(std::move(fullscreenHandler))
    {
        // Its own visibility is the strip's to decide: shown once a score is
        // open. Added without being made visible, so this does not override it.
        addChildComponent(transportStrip);
        transportStrip.setVisible(transport.isVisible());

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
        hamburgerButton.setButtonText(l10n::text("title.menu"));
        hamburgerButton.setTooltip(l10n::text("title.menu.tooltip"));
        hamburgerButton.setComponentID("hamburger-button");
        hamburgerButton.onClick = [this] { showCollapsedMenu(); };
        addChildComponent(hamburgerButton);

        fullscreenButton.setTooltip(l10n::text("title.fullscreen.enter.tooltip"));
        fullscreenButton.setComponentID("fullscreen-button");
        fullscreenButton.onClick = onFullscreen;
        addAndMakeVisible(fullscreenButton);

        setFullscreen(false);
    }

    void setFullscreen(bool isFullscreen)
    {
        fullscreenButton.setButtonText(l10n::text(
            isFullscreen ? l10n::key("title.fullscreen.restore") : l10n::key("title.fullscreen")));
        fullscreenButton.setTooltip(l10n::text(
            isFullscreen ? l10n::key("title.fullscreen.exit.tooltip")
                         : l10n::key("title.fullscreen.enter.tooltip")));
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
        // Widths for text at 100 percent, grown with the text size: at 150 the
        // menus collapse into one button sooner, so the strip still fits.
        const auto px = [this](int base) { return appearance::scaledFor(*this, base); };
        auto bounds = getLocalBounds().reduced(6, 5);
        const int menuWidth = px(96);
        constexpr int gap = 4;
        const int windowButtonWidth = px(76);

        fullscreenButton.setBounds(bounds.removeFromRight(windowButtonWidth));
        bounds.removeFromRight(gap);

        const auto useCollapsedMenu = getWidth() < px(900);
        hamburgerButton.setVisible(useCollapsedMenu);
        for (auto* button :
             {&fileButton, &settingsButton, &toolsButton, &helpButton, &microphoneButton})
        {
            button->setVisible(!useCollapsedMenu);
        }

        constexpr int transportGap = 12;
        if (useCollapsedMenu)
        {
            hamburgerButton.setBounds(bounds.removeFromLeft(px(76)));
            bounds.removeFromLeft(transportGap);
        }
        else
        {
            microphoneButton.setBounds(bounds.removeFromRight(px(190)));
            bounds.removeFromRight(transportGap);

            // While the transport shows, the menus give up width before it
            // drops below its compact size: at the narrowest window there is
            // room for both only if the menu buttons narrow a little.
            int width = menuWidth;
            if (transportStrip.isVisible())
            {
                const int menusAtFull = 4 * menuWidth + 3 * gap + transportGap;
                const int shortfall =
                    px(TransportStrip::compactWidth) - (bounds.getWidth() - menusAtFull);
                if (shortfall > 0)
                {
                    width = std::max(px(64), menuWidth - (shortfall + 3) / 4);
                }
            }

            fileButton.setBounds(bounds.removeFromLeft(width));
            bounds.removeFromLeft(gap);
            settingsButton.setBounds(bounds.removeFromLeft(width));
            bounds.removeFromLeft(gap);
            toolsButton.setBounds(bounds.removeFromLeft(width));
            bounds.removeFromLeft(gap);
            helpButton.setBounds(bounds.removeFromLeft(width));
            bounds.removeFromLeft(transportGap);
        }

        const int available = bounds.getWidth();
        const int stripWidth = available >= px(TransportStrip::fullWidth)
                                   ? px(TransportStrip::fullWidth)
                                   : std::min(available, px(TransportStrip::compactWidth));
        transportStrip.setBounds(bounds.removeFromLeft(std::max(0, stripWidth)));
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
        menu.addItem(file, l10n::text("title.file"), false);
        menu.addItem(settings, l10n::text("title.settings"));
        menu.addItem(tools, l10n::text("title.tools"));
        menu.addItem(help, l10n::text("title.help"));
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
    TransportStrip& transportStrip;
    juce::TextButton hamburgerButton;
    juce::TextButton fullscreenButton;
    std::function<void()> onFullscreen;
};
