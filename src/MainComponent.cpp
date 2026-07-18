#include "MainComponent.h"

#include "SpectrogramComponent.h"
#include "TunerComponent.h"

#include <functional>
#include <utility>

namespace
{
struct Palette
{
    juce::Colour background;
    juce::Colour panel;
    juce::Colour button;
    juce::Colour buttonHover;
    juce::Colour foreground;
    juce::Colour muted;
    juce::Colour outline;
    juce::Colour accent;
    juce::Colour warning;
};

Palette paletteFor(bool darkMode)
{
    if (darkMode)
    {
        return {
            juce::Colour::fromRGB(18, 20, 27),
            juce::Colour::fromRGB(30, 33, 42),
            juce::Colour::fromRGB(54, 59, 72),
            juce::Colour::fromRGB(70, 76, 92),
            juce::Colour::fromRGB(238, 241, 247),
            juce::Colour::fromRGB(158, 166, 181),
            juce::Colour::fromRGB(78, 85, 103),
            juce::Colour::fromRGB(100, 170, 255),
            juce::Colour::fromRGB(244, 178, 73)
        };
    }

    return {
        juce::Colour::fromRGB(235, 236, 238),
        juce::Colour::fromRGB(250, 250, 251),
        juce::Colour::fromRGB(244, 244, 245),
        juce::Colour::fromRGB(225, 228, 233),
        juce::Colour::fromRGB(28, 31, 37),
        juce::Colour::fromRGB(92, 98, 108),
        juce::Colour::fromRGB(165, 169, 178),
        juce::Colour::fromRGB(55, 112, 196),
        juce::Colour::fromRGB(172, 103, 18)
    };
}
}

class MainComponent::ToolWindow final : public juce::DocumentWindow
{
public:
    ToolWindow(const juce::String& title,
               std::unique_ptr<juce::Component> content,
               int preferredWidth,
               int preferredHeight,
               std::function<void()> closeHandler)
        : DocumentWindow(title,
                         juce::Colours::darkgrey,
                         juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content.release(), true);
        setResizable(true, true);
        setResizeLimits(520, 420, 2400, 1600);
        centreWithSize(preferredWidth, preferredHeight);
        setVisible(true);
    }

    ~ToolWindow() override
    {
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        const auto callback = onClose;
        juce::MessageManager::callAsync([callback]
        {
            if (callback)
                callback();
        });
    }

    void applyAppearance(juce::LookAndFeel* lookAndFeel,
                         juce::Colour background,
                         bool darkMode)
    {
        setLookAndFeel(lookAndFeel);
        setBackgroundColour(background);

        if (auto* tuner = dynamic_cast<TunerComponent*>(getContentComponent()))
            tuner->setDarkMode(darkMode);
        if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(getContentComponent()))
            spectrogram->setDarkMode(darkMode);

        sendLookAndFeelChange();
        repaint();
    }

private:
    std::function<void()> onClose;
};

class MainComponent::SettingsWindow final : public juce::DocumentWindow
{
public:
    class Content final : public juce::Component
    {
    public:
        Content(juce::AudioDeviceManager& audioDeviceManager,
                bool initiallyDark,
                std::function<void(bool)> appearanceHandler)
            : deviceSelector(audioDeviceManager,
                             1, 2,
                             0, 0,
                             false, false,
                             false, true),
              onAppearanceChanged(std::move(appearanceHandler))
        {
            appearanceHeading.setText("Appearance", juce::dontSendNotification);
            appearanceHeading.setFont(juce::FontOptions(18.0f, juce::Font::bold));
            addAndMakeVisible(appearanceHeading);

            appearanceLabel.setText("Theme", juce::dontSendNotification);
            addAndMakeVisible(appearanceLabel);

            appearanceBox.addItem("Light", 1);
            appearanceBox.addItem("Dark", 2);
            appearanceBox.setSelectedId(initiallyDark ? 2 : 1,
                                        juce::dontSendNotification);
            appearanceBox.onChange = [this]
            {
                if (onAppearanceChanged)
                    onAppearanceChanged(appearanceBox.getSelectedId() == 2);
            };
            addAndMakeVisible(appearanceBox);

            audioHeading.setText("Audio", juce::dontSendNotification);
            audioHeading.setFont(juce::FontOptions(18.0f, juce::Font::bold));
            addAndMakeVisible(audioHeading);
            addAndMakeVisible(deviceSelector);
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(22);

            appearanceHeading.setBounds(bounds.removeFromTop(30));
            bounds.removeFromTop(8);

            auto appearanceRow = bounds.removeFromTop(34);
            appearanceLabel.setBounds(appearanceRow.removeFromLeft(130));
            appearanceBox.setBounds(appearanceRow.removeFromLeft(220));

            bounds.removeFromTop(24);
            audioHeading.setBounds(bounds.removeFromTop(30));
            bounds.removeFromTop(8);
            deviceSelector.setBounds(bounds);
        }

        void setDarkMode(bool darkMode)
        {
            appearanceBox.setSelectedId(darkMode ? 2 : 1,
                                        juce::dontSendNotification);
            sendLookAndFeelChange();
            repaint();
        }

    private:
        juce::Label appearanceHeading;
        juce::Label appearanceLabel;
        juce::ComboBox appearanceBox;
        juce::Label audioHeading;
        juce::AudioDeviceSelectorComponent deviceSelector;
        std::function<void(bool)> onAppearanceChanged;
    };

    SettingsWindow(juce::AudioDeviceManager& audioDeviceManager,
                   bool initiallyDark,
                   std::function<void(bool)> appearanceHandler,
                   std::function<void()> closeHandler)
        : DocumentWindow("Settings",
                         juce::Colours::darkgrey,
                         juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Content(audioDeviceManager,
                                    initiallyDark,
                                    std::move(appearanceHandler)),
                        true);
        setResizable(true, true);
        setResizeLimits(620, 500, 1300, 1000);
        centreWithSize(760, 650);
        setVisible(true);
    }

    ~SettingsWindow() override
    {
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        const auto callback = onClose;
        juce::MessageManager::callAsync([callback]
        {
            if (callback)
                callback();
        });
    }

    void applyAppearance(juce::LookAndFeel* lookAndFeel,
                         juce::Colour background,
                         bool darkMode)
    {
        setLookAndFeel(lookAndFeel);
        setBackgroundColour(background);
        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
            content->setDarkMode(darkMode);
        sendLookAndFeelChange();
        repaint();
    }

private:
    std::function<void()> onClose;
};

class MainComponent::MicrophoneWarning final : public juce::Component
{
public:
    MicrophoneWarning(std::function<void()> settingsHandler,
                      std::function<void()> dismissHandler)
        : onOpenSettings(std::move(settingsHandler)),
          onDismiss(std::move(dismissHandler))
    {
        setInterceptsMouseClicks(true, true);

        title.setText("No microphone detected", juce::dontSendNotification);
        title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        addAndMakeVisible(title);

        message.setText("Choose an input device in Settings to use the tuner and spectrogram.",
                        juce::dontSendNotification);
        message.setFont(juce::FontOptions(13.0f));
        message.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(message);

        settingsButton.setButtonText("Open Settings");
        settingsButton.onClick = [this]
        {
            if (onOpenSettings)
                onOpenSettings();
        };
        addAndMakeVisible(settingsButton);

        dismissButton.setButtonText("Dismiss");
        dismissButton.onClick = [this]
        {
            if (onDismiss)
                onDismiss();
        };
        addAndMakeVisible(dismissButton);
    }

    void setDarkMode(bool shouldUseDarkMode)
    {
        darkMode = shouldUseDarkMode;
        const auto palette = paletteFor(darkMode);
        title.setColour(juce::Label::textColourId, palette.foreground);
        message.setColour(juce::Label::textColourId, palette.muted);
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto palette = paletteFor(darkMode);
        const auto card = getLocalBounds().toFloat().reduced(6.0f);

        graphics.setColour(juce::Colours::black.withAlpha(darkMode ? 0.35f : 0.14f));
        graphics.fillRoundedRectangle(card.translated(0.0f, 3.0f), 13.0f);

        graphics.setColour(palette.panel);
        graphics.fillRoundedRectangle(card, 13.0f);
        graphics.setColour(palette.warning.withAlpha(0.95f));
        graphics.fillRoundedRectangle(card.withWidth(5.0f), 3.0f);
        graphics.setColour(palette.outline.withAlpha(0.85f));
        graphics.drawRoundedRectangle(card, 13.0f, 1.0f);

        graphics.setColour(palette.warning);
        graphics.fillEllipse(20.0f, 23.0f, 24.0f, 24.0f);
        graphics.setColour(darkMode ? juce::Colour::fromRGB(35, 29, 18)
                                    : juce::Colours::white);
        graphics.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        graphics.drawText("!", 20, 22, 24, 25, juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18, 14);
        bounds.removeFromLeft(42);

        auto buttons = bounds.removeFromBottom(30);
        dismissButton.setBounds(buttons.removeFromRight(88));
        buttons.removeFromRight(8);
        settingsButton.setBounds(buttons.removeFromRight(122));

        title.setBounds(bounds.removeFromTop(24));
        message.setBounds(bounds);
    }

private:
    juce::Label title;
    juce::Label message;
    juce::TextButton settingsButton;
    juce::TextButton dismissButton;
    std::function<void()> onOpenSettings;
    std::function<void()> onDismiss;
    bool darkMode = false;
};

MainComponent::MainComponent()
{
    setOpaque(true);

    addAndMakeVisible(fileButton);
    addAndMakeVisible(settingsButton);
    addAndMakeVisible(toolsButton);

    fileButton.setTooltip("File actions will be added later.");
    settingsButton.onClick = [this] { showSettings(); };
    toolsButton.onClick = [this] { showToolsMenu(); };

    microphoneWarning = std::make_unique<MicrophoneWarning>(
        [this] { showSettings(); },
        [this] { dismissMicrophoneWarning(); });
    addChildComponent(*microphoneWarning);

    juce::XmlElement noDeviceState { "DEVICESETUP" };
    audioDeviceManager.initialise(2, 0, &noDeviceState, false);
    audioDeviceManager.addChangeListener(this);

    applyAppearance();
    updateMicrophoneWarning();
    setSize(1200, 760);
}

MainComponent::~MainComponent()
{
    audioDeviceManager.removeChangeListener(this);
    settingsWindow.reset();
    spectrogramWindow.reset();
    tunerWindow.reset();
    microphoneWarning.reset();
    setLookAndFeel(nullptr);
    audioDeviceManager.closeAudioDevice();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    const auto palette = paletteFor(darkMode);
    graphics.fillAll(palette.background);

    const auto barTop = darkMode
        ? juce::Colour::fromRGB(8, 9, 12)
        : juce::Colour::fromRGB(38, 39, 42);
    const auto barBottom = darkMode
        ? palette.panel
        : juce::Colour::fromRGB(160, 162, 167);

    juce::ColourGradient menuGradient(
        barTop,
        static_cast<float>(menuBarBounds.getCentreX()),
        static_cast<float>(menuBarBounds.getY()),
        barBottom,
        static_cast<float>(menuBarBounds.getCentreX()),
        static_cast<float>(menuBarBounds.getBottom()),
        false);
    graphics.setGradientFill(menuGradient);
    graphics.fillRect(menuBarBounds);

    graphics.setColour(palette.outline.withAlpha(0.7f));
    graphics.drawHorizontalLine(menuBarBounds.getBottom() - 1,
                                0.0f,
                                static_cast<float>(getWidth()));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    menuBarBounds = bounds.removeFromTop(40);

    auto menu = menuBarBounds.reduced(4, 5);
    constexpr int buttonWidth = 96;
    constexpr int gap = 4;

    fileButton.setBounds(menu.removeFromLeft(buttonWidth));
    menu.removeFromLeft(gap);
    settingsButton.setBounds(menu.removeFromLeft(buttonWidth));
    menu.removeFromLeft(gap);
    toolsButton.setBounds(menu.removeFromLeft(buttonWidth));

    if (microphoneWarning != nullptr)
    {
        auto warningArea = bounds.reduced(18);
        microphoneWarning->setBounds(
            warningArea.removeFromTop(118).removeFromRight(
                juce::jmin(470, warningArea.getWidth())));
    }
}

void MainComponent::showToolsMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addItem(1, "Tuner", true, tunerWindow != nullptr);
    menu.addItem(2, "Spectrogram", true, spectrogramWindow != nullptr);

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetComponent(&toolsButton)
            .withMinimumWidth(190),
        [safeThis](int result)
        {
            if (safeThis == nullptr)
                return;

            if (result == 1)
                safeThis->openTool(ToolType::tuner);
            else if (result == 2)
                safeThis->openTool(ToolType::spectrogram);
        });
}

void MainComponent::openTool(ToolType tool)
{
    auto& window = tool == ToolType::tuner ? tunerWindow : spectrogramWindow;

    if (window != nullptr)
    {
        window->setVisible(true);
        window->toFront(true);
        return;
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    const auto closeHandler = [safeThis, tool]
    {
        if (safeThis != nullptr)
            safeThis->closeTool(tool);
    };

    if (tool == ToolType::tuner)
    {
        auto content = std::make_unique<TunerComponent>(audioDeviceManager);
        content->setDarkMode(darkMode);
        window = std::make_unique<ToolWindow>(
            "Tuner",
            std::move(content),
            920,
            760,
            closeHandler);
    }
    else
    {
        auto content = std::make_unique<SpectrogramComponent>(audioDeviceManager);
        content->setDarkMode(darkMode);
        window = std::make_unique<ToolWindow>(
            "Spectrogram",
            std::move(content),
            980,
            650,
            closeHandler);
    }

    const auto palette = paletteFor(darkMode);
    window->applyAppearance(&appLookAndFeel, palette.background, darkMode);
    window->toFront(true);
}

void MainComponent::closeTool(ToolType tool)
{
    if (tool == ToolType::tuner)
        tunerWindow.reset();
    else
        spectrogramWindow.reset();
}

void MainComponent::showSettings()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->setVisible(true);
        settingsWindow->toFront(true);
        return;
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    settingsWindow = std::make_unique<SettingsWindow>(
        audioDeviceManager,
        darkMode,
        [safeThis](bool shouldUseDarkMode)
        {
            if (safeThis != nullptr)
                safeThis->setDarkMode(shouldUseDarkMode);
        },
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->closeSettings();
        });

    const auto palette = paletteFor(darkMode);
    settingsWindow->applyAppearance(&appLookAndFeel,
                                    palette.background,
                                    darkMode);
    settingsWindow->toFront(true);
}

void MainComponent::closeSettings()
{
    settingsWindow.reset();
    updateMicrophoneWarning();
}

void MainComponent::setDarkMode(bool shouldUseDarkMode)
{
    if (darkMode == shouldUseDarkMode)
        return;

    darkMode = shouldUseDarkMode;
    applyAppearance();
}

void MainComponent::applyAppearance()
{
    const auto palette = paletteFor(darkMode);

    appLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId,
                             palette.background);
    appLookAndFeel.setColour(juce::DocumentWindow::textColourId,
                             palette.foreground);
    appLookAndFeel.setColour(juce::Label::textColourId,
                             palette.foreground);
    appLookAndFeel.setColour(juce::Label::backgroundColourId,
                             juce::Colours::transparentBlack);
    appLookAndFeel.setColour(juce::Label::outlineColourId,
                             juce::Colours::transparentBlack);

    appLookAndFeel.setColour(juce::TextButton::buttonColourId,
                             palette.button);
    appLookAndFeel.setColour(juce::TextButton::buttonOnColourId,
                             palette.buttonHover);
    appLookAndFeel.setColour(juce::TextButton::textColourOffId,
                             palette.foreground);
    appLookAndFeel.setColour(juce::TextButton::textColourOnId,
                             palette.foreground);

    appLookAndFeel.setColour(juce::ComboBox::backgroundColourId,
                             palette.button);
    appLookAndFeel.setColour(juce::ComboBox::textColourId,
                             palette.foreground);
    appLookAndFeel.setColour(juce::ComboBox::outlineColourId,
                             palette.outline);
    appLookAndFeel.setColour(juce::ComboBox::arrowColourId,
                             palette.foreground);

    appLookAndFeel.setColour(juce::PopupMenu::backgroundColourId,
                             palette.panel);
    appLookAndFeel.setColour(juce::PopupMenu::textColourId,
                             palette.foreground);
    appLookAndFeel.setColour(juce::PopupMenu::headerTextColourId,
                             palette.muted);
    appLookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId,
                             palette.accent.withAlpha(0.7f));
    appLookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId,
                             palette.foreground);

    appLookAndFeel.setColour(juce::Slider::backgroundColourId,
                             palette.panel);
    appLookAndFeel.setColour(juce::Slider::trackColourId,
                             palette.accent.withAlpha(0.75f));
    appLookAndFeel.setColour(juce::Slider::thumbColourId,
                             palette.accent);
    appLookAndFeel.setColour(juce::Slider::textBoxTextColourId,
                             palette.foreground);
    appLookAndFeel.setColour(juce::Slider::textBoxBackgroundColourId,
                             palette.button);
    appLookAndFeel.setColour(juce::Slider::textBoxOutlineColourId,
                             palette.outline);

    appLookAndFeel.setColour(juce::ToggleButton::textColourId,
                             palette.foreground);
    appLookAndFeel.setColour(juce::ToggleButton::tickColourId,
                             palette.accent);
    appLookAndFeel.setColour(juce::ToggleButton::tickDisabledColourId,
                             palette.muted);

    setLookAndFeel(&appLookAndFeel);

    for (auto* button : { &fileButton, &settingsButton, &toolsButton })
    {
        button->setColour(juce::TextButton::buttonColourId, palette.button);
        button->setColour(juce::TextButton::buttonOnColourId,
                          palette.buttonHover);
        button->setColour(juce::TextButton::textColourOffId,
                          palette.foreground);
        button->setColour(juce::TextButton::textColourOnId,
                          palette.foreground);
    }

    if (microphoneWarning != nullptr)
        microphoneWarning->setDarkMode(darkMode);
    if (tunerWindow != nullptr)
        tunerWindow->applyAppearance(&appLookAndFeel,
                                     palette.background,
                                     darkMode);
    if (spectrogramWindow != nullptr)
        spectrogramWindow->applyAppearance(&appLookAndFeel,
                                           palette.background,
                                           darkMode);
    if (settingsWindow != nullptr)
        settingsWindow->applyAppearance(&appLookAndFeel,
                                        palette.background,
                                        darkMode);

    sendLookAndFeelChange();
    repaint();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioDeviceManager)
        updateMicrophoneWarning();
}

bool MainComponent::hasUsableMicrophone() const
{
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    return device != nullptr
        && device->isOpen()
        && device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

void MainComponent::updateMicrophoneWarning()
{
    if (microphoneWarning == nullptr)
        return;

    if (hasUsableMicrophone())
    {
        microphoneWarningDismissed = false;
        microphoneWarning->setVisible(false);
        return;
    }

    microphoneWarning->setVisible(! microphoneWarningDismissed);
    if (microphoneWarning->isVisible())
        microphoneWarning->toFront(false);
}

void MainComponent::dismissMicrophoneWarning()
{
    microphoneWarningDismissed = true;
    if (microphoneWarning != nullptr)
        microphoneWarning->setVisible(false);
}
