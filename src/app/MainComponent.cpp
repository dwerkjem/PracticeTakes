#include "../MainComponent.h"

#include "../SpectrogramComponent.h"
#include "../TunerComponent.h"

#include <functional>
#include <utility>

namespace
{
constexpr int menuBarHeight = 40;
constexpr int menuButtonWidth = 96;
constexpr int menuButtonGap = 4;
constexpr int toolsMenuWidth = 190;
constexpr int helpMenuWidth = 190;

constexpr int microphoneWarningWidth = 470;
constexpr int microphoneWarningHeight = 118;

constexpr int tunerMenuItemId = 1;
constexpr int spectrogramMenuItemId = 2;
constexpr int sendFeedbackMenuItemId = 1;
constexpr int lightThemeId = static_cast<int>(Theme::light);
constexpr int darkThemeId = static_cast<int>(Theme::dark);

constexpr auto settingsSchemaKey = "settings.schema";
constexpr auto themeKey = "global.theme";
constexpr auto audioStateKey = "audio.deviceState";
constexpr auto tunerEasingKey = "tuner.easing";
constexpr auto tunerAveragingKey = "tuner.averaging";
constexpr auto tunerThresholdKey = "tuner.noteSwitch";
constexpr auto tunerDropoutKey = "tuner.dropout";
constexpr auto tunerDurationKey = "tuner.graphDuration";
constexpr auto tunerBoundsKey = "layout.tuner";
constexpr auto spectrogramBoundsKey = "layout.spectrogram";
constexpr auto settingsBoundsKey = "layout.settings";

// Window close handlers delete their owning unique_ptr. Running the callback
// asynchronously prevents a window from deleting itself inside its own close
// button callback.
void invokeLater(std::function<void()> callback)
{
    juce::MessageManager::callAsync(
        [callback = std::move(callback)]() mutable
        {
            if (callback)
            {
                callback();
            }
        });
}
} // namespace

//==============================================================================
// Generic host window used by all analysis tools.
class MainComponent::ToolWindow final : public juce::DocumentWindow
{
  public:
    ToolWindow(const juce::String& title, std::unique_ptr<juce::Component> content,
               juce::Point<int> preferredSize, std::function<void()> closeHandler)
        : DocumentWindow(title, juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content.release(), true);
        setResizable(true, true);
        setResizeLimits(520, 420, 2400, 1600);
        centreWithSize(preferredSize.x, preferredSize.y);
        setVisible(true);
    }

    ~ToolWindow() override
    {
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        invokeLater(onClose);
    }

    void applyAppearance(juce::LookAndFeel* lookAndFeel, juce::Colour background, Theme theme)
    {
        setLookAndFeel(lookAndFeel);
        setBackgroundColour(background);

        // Tool components draw their own graphs and panels, so they receive
        // the selected appearance in addition to the shared LookAndFeel.
        if (auto* tuner = dynamic_cast<TunerComponent*>(getContentComponent()))
        {
            tuner->setTheme(theme);
        }

        if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(getContentComponent()))
        {
            spectrogram->setTheme(theme);
        }

        sendLookAndFeelChange();
        repaint();
    }

  private:
    std::function<void()> onClose;
};

//==============================================================================
// Settings remains separate from the main window so it can grow without
// crowding the top-level application shell.
class MainComponent::SettingsWindow final : public juce::DocumentWindow
{
  public:
    class Content final : public juce::Component
    {
      public:
        Content(juce::AudioDeviceManager& audioDeviceManager, Theme initialTheme,
                std::function<void(Theme)> appearanceHandler,
                std::function<void(AppDefaults::Preset)> presetHandler,
                std::function<void()> saveHandler, std::function<void()> feedbackHandler,
                std::function<void()> resetToolHandler, std::function<void()> resetAudioHandler,
                std::function<void()> resetLayoutHandler, std::function<void()> resetAllHandler)
            : deviceSelector(audioDeviceManager, 1, 2, 0, 0, false, false, false, true),
              onAppearanceChanged(std::move(appearanceHandler)), onPreset(std::move(presetHandler))
        {
            configureHeading(appearanceHeading, "Appearance");

            appearanceLabel.setText("Theme", juce::dontSendNotification);
            addAndMakeVisible(appearanceLabel);

            appearanceBox.addItem("Light", lightThemeId);
            appearanceBox.addItem("Dark", darkThemeId);
            appearanceBox.setSelectedId(static_cast<int>(initialTheme), juce::dontSendNotification);
            appearanceBox.onChange = [this]
            {
                if (onAppearanceChanged)
                {
                    onAppearanceChanged(static_cast<Theme>(appearanceBox.getSelectedId()));
                }
            };
            addAndMakeVisible(appearanceBox);

            configureHeading(audioHeading, "Audio");
            addAndMakeVisible(deviceSelector);

            configureHeading(presetsHeading, "Practice preset");
            presetBox.addItem("Voice practice", static_cast<int>(AppDefaults::Preset::voice));
            presetBox.addItem("General instrument",
                              static_cast<int>(AppDefaults::Preset::generalInstrument));
            presetBox.setTextWhenNothingSelected("Choose a preset");
            presetBox.onChange = [this]
            {
                if (onPreset)
                    onPreset(static_cast<AppDefaults::Preset>(presetBox.getSelectedId()));
            };
            addAndMakeVisible(presetBox);

            configureHeading(resetHeading, "Reset");
            saveButton.setButtonText("Save settings");
            saveButton.onClick = std::move(saveHandler);
            addAndMakeVisible(saveButton);
            feedbackButton.setButtonText("Send feedback");
            feedbackButton.setTitle("Open the feedback form");
            feedbackButton.onClick = std::move(feedbackHandler);
            addAndMakeVisible(feedbackButton);
            configureResetButton(resetToolButton, "Current tool", std::move(resetToolHandler));
            configureResetButton(resetAudioButton, "Audio", std::move(resetAudioHandler));
            configureResetButton(resetLayoutButton, "Layout", std::move(resetLayoutHandler));
            configureResetButton(resetAllButton, "All settings", std::move(resetAllHandler));
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
            auto resetArea = bounds.removeFromBottom(186);
            auto presetArea = bounds.removeFromBottom(92);
            deviceSelector.setBounds(bounds);

            presetsHeading.setBounds(presetArea.removeFromTop(30));
            presetArea.removeFromTop(6);
            presetBox.setBounds(presetArea.removeFromTop(34).removeFromLeft(260));

            resetHeading.setBounds(resetArea.removeFromTop(30));
            feedbackButton.setBounds(resetHeading.getRight() - 150, resetHeading.getY(), 150, 30);
            resetArea.removeFromTop(6);
            saveButton.setBounds(resetArea.removeFromTop(34).removeFromLeft(180));
            resetArea.removeFromTop(8);
            auto firstRow = resetArea.removeFromTop(34);
            resetToolButton.setBounds(firstRow.removeFromLeft(150));
            firstRow.removeFromLeft(8);
            resetAudioButton.setBounds(firstRow.removeFromLeft(150));
            resetArea.removeFromTop(8);
            auto secondRow = resetArea.removeFromTop(34);
            resetLayoutButton.setBounds(secondRow.removeFromLeft(150));
            secondRow.removeFromLeft(8);
            resetAllButton.setBounds(secondRow.removeFromLeft(150));
        }

        void setTheme(Theme theme)
        {
            appearanceBox.setSelectedId(static_cast<int>(theme), juce::dontSendNotification);
            sendLookAndFeelChange();
            repaint();
        }

      private:
        void configureHeading(juce::Label& heading, const juce::String& text)
        {
            heading.setText(text, juce::dontSendNotification);
            heading.setFont(juce::FontOptions(18.0f, juce::Font::bold));
            addAndMakeVisible(heading);
        }

        void configureResetButton(juce::TextButton& button, const juce::String& text,
                                  std::function<void()> action)
        {
            button.setButtonText(text);
            button.onClick = [action = std::move(action), text]
            {
                juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::WarningIcon, "Confirm reset",
                    "Reset " + text.toLowerCase() + "? This cannot be undone.", "Reset", "Cancel",
                    nullptr,
                    juce::ModalCallbackFunction::create(
                        [action](int result)
                        {
                            if (result != 0 && action)
                                action();
                        }));
            };
            addAndMakeVisible(button);
        }

        juce::Label appearanceHeading;
        juce::Label appearanceLabel;
        juce::ComboBox appearanceBox;
        juce::Label audioHeading;
        juce::AudioDeviceSelectorComponent deviceSelector;
        juce::Label presetsHeading;
        juce::ComboBox presetBox;
        juce::Label resetHeading;
        juce::TextButton saveButton;
        juce::TextButton feedbackButton;
        juce::TextButton resetToolButton;
        juce::TextButton resetAudioButton;
        juce::TextButton resetLayoutButton;
        juce::TextButton resetAllButton;
        std::function<void(Theme)> onAppearanceChanged;
        std::function<void(AppDefaults::Preset)> onPreset;
    };

    SettingsWindow(juce::AudioDeviceManager& audioDeviceManager, Theme initialTheme,
                   std::function<void(Theme)> appearanceHandler,
                   std::function<void(AppDefaults::Preset)> presetHandler,
                   std::function<void()> saveHandler, std::function<void()> feedbackHandler,
                   std::function<void()> resetToolHandler, std::function<void()> resetAudioHandler,
                   std::function<void()> resetLayoutHandler, std::function<void()> resetAllHandler,
                   std::function<void()> closeHandler)
        : DocumentWindow("Settings", juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Content(audioDeviceManager, initialTheme, std::move(appearanceHandler),
                                    std::move(presetHandler), std::move(saveHandler),
                                    std::move(feedbackHandler), std::move(resetToolHandler),
                                    std::move(resetAudioHandler), std::move(resetLayoutHandler),
                                    std::move(resetAllHandler)),
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
        invokeLater(onClose);
    }

    void applyAppearance(juce::LookAndFeel* lookAndFeel, juce::Colour background, Theme theme)
    {
        setLookAndFeel(lookAndFeel);
        setBackgroundColour(background);

        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        {
            content->setTheme(theme);
        }

        sendLookAndFeelChange();
        repaint();
    }

  private:
    std::function<void()> onClose;
};

//==============================================================================
class MainComponent::FeedbackWindow final : public juce::DocumentWindow
{
  public:
    FeedbackWindow(juce::PropertiesFile& propertiesFile, std::function<void()> closeHandler)
        : DocumentWindow("Send feedback", juce::Colours::darkgrey,
                         juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new FeedbackComponent(propertiesFile), true);
        setResizable(true, true);
        setResizeLimits(620, 700, 1200, 1100);
        centreWithSize(760, 780);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        invokeLater(onClose);
    }

  private:
    std::function<void()> onClose;
};

//==============================================================================
// A custom, nonmodal warning card. It stays inside the main window so missing
// audio hardware does not interrupt the user with a native modal dialog.
class MainComponent::MicrophoneWarning final : public juce::Component
{
  public:
    MicrophoneWarning(std::function<void()> settingsHandler, std::function<void()> dismissHandler)
        : onOpenSettings(std::move(settingsHandler)), onDismiss(std::move(dismissHandler))
    {
        setInterceptsMouseClicks(true, true);

        title.setText("No microphone detected", juce::dontSendNotification);
        title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        addAndMakeVisible(title);

        message.setText("Choose an input device in Settings to use the tuner and "
                        "spectrogram.",
                        juce::dontSendNotification);
        message.setFont(juce::FontOptions(13.0f));
        message.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(message);

        settingsButton.setButtonText("Open Settings");
        settingsButton.onClick = [this]
        {
            if (onOpenSettings)
            {
                onOpenSettings();
            }
        };
        addAndMakeVisible(settingsButton);

        dismissButton.setButtonText("Dismiss");
        dismissButton.onClick = [this]
        {
            if (onDismiss)
            {
                onDismiss();
            }
        };
        addAndMakeVisible(dismissButton);
    }

    void setTheme(Theme theme)
    {
        currentTheme = theme;
        const auto palette = appPaletteFor(currentTheme);

        title.setColour(juce::Label::textColourId, palette.foreground);
        message.setColour(juce::Label::textColourId, palette.muted);
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto palette = appPaletteFor(currentTheme);
        const auto card = getLocalBounds().toFloat().reduced(6.0f);

        // A subtle shadow and rounded panel make the warning visible without
        // blocking or visually dominating the application.
        graphics.setColour(
            juce::Colours::black.withAlpha(isDarkTheme(currentTheme) ? 0.35f : 0.14f));
        graphics.fillRoundedRectangle(card.translated(0.0f, 3.0f), 13.0f);

        graphics.setColour(palette.panel);
        graphics.fillRoundedRectangle(card, 13.0f);

        graphics.setColour(palette.warning.withAlpha(0.95f));
        graphics.fillRoundedRectangle(card.withWidth(5.0f), 3.0f);

        graphics.setColour(palette.outline.withAlpha(0.85f));
        graphics.drawRoundedRectangle(card, 13.0f, 1.0f);

        graphics.setColour(palette.warning);
        graphics.fillEllipse(20.0f, 23.0f, 24.0f, 24.0f);

        graphics.setColour(isDarkTheme(currentTheme) ? juce::Colour::fromRGB(35, 29, 18)
                                                     : juce::Colours::white);
        graphics.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        graphics.drawText("!", 20, 22, 24, 25, juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18, 14);
        bounds.removeFromLeft(42); // Reserve room for the warning icon.

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
    Theme currentTheme = Theme::light;
};

//==============================================================================
MainComponent::MainComponent()
{
    setOpaque(true);
    audioInputService.addChangeListener(this);

    juce::PropertiesFile::Options storageOptions;
    storageOptions.applicationName = "PracticeTakes";
    storageOptions.filenameSuffix = ".settings";
    storageOptions.folderName = "PracticeTakes";
    storageOptions.osxLibrarySubFolder = "Application Support";
    storageOptions.commonToAllUsers = false;
    applicationProperties.setStorageParameters(storageOptions);
    loadSettings();

    configureTopButtons();
    createMicrophoneWarning();
    applyAppearance();
    updateMicrophoneWarning();
    setSize(1200, 760);
}

MainComponent::~MainComponent()
{
    audioInputService.removeChangeListener(this);
    // Tool components unregister from the service in their destructors, so
    // close the windows before the service is destroyed.
    settingsWindow.reset();
    feedbackWindow.reset();
    spectrogramWindow.reset();
    tunerWindow.reset();
    microphoneWarning.reset();

    applicationProperties.closeFiles();

    setLookAndFeel(nullptr);
}

void MainComponent::configureTopButtons()
{
    addAndMakeVisible(fileButton);
    addAndMakeVisible(settingsButton);
    addAndMakeVisible(toolsButton);
    addAndMakeVisible(helpButton);

    // File is intentionally present but inactive while the project/file model
    // is still being designed.
    fileButton.setTooltip("File actions will be added later.");
    settingsButton.onClick = [this] { showSettings(); };
    toolsButton.onClick = [this] { showToolsMenu(); };
    helpButton.onClick = [this] { showHelpMenu(); };
}

void MainComponent::createMicrophoneWarning()
{
    microphoneWarning = std::make_unique<MicrophoneWarning>([this] { showSettings(); },
                                                            [this] { dismissMicrophoneWarning(); });

    // Start hidden; updateMicrophoneWarning decides whether it is needed.
    addChildComponent(*microphoneWarning);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    const auto palette = appPaletteFor(currentTheme);
    graphics.fillAll(palette.background);

    juce::ColourGradient menuGradient(
        palette.menuBarTop, static_cast<float>(menuBarBounds.getCentreX()),
        static_cast<float>(menuBarBounds.getY()), palette.menuBarBottom,
        static_cast<float>(menuBarBounds.getCentreX()),
        static_cast<float>(menuBarBounds.getBottom()), false);

    graphics.setGradientFill(menuGradient);
    graphics.fillRect(menuBarBounds);

    graphics.setColour(palette.outline.withAlpha(0.7f));
    graphics.drawHorizontalLine(menuBarBounds.getBottom() - 1, 0.0f,
                                static_cast<float>(getWidth()));
}

void MainComponent::resized()
{
    auto remainingBounds = getLocalBounds();
    menuBarBounds = remainingBounds.removeFromTop(menuBarHeight);

    auto menuBounds = menuBarBounds.reduced(4, 5);
    fileButton.setBounds(menuBounds.removeFromLeft(menuButtonWidth));
    menuBounds.removeFromLeft(menuButtonGap);
    settingsButton.setBounds(menuBounds.removeFromLeft(menuButtonWidth));
    menuBounds.removeFromLeft(menuButtonGap);
    toolsButton.setBounds(menuBounds.removeFromLeft(menuButtonWidth));
    menuBounds.removeFromLeft(menuButtonGap);
    helpButton.setBounds(menuBounds.removeFromLeft(menuButtonWidth));

    if (microphoneWarning != nullptr)
    {
        auto warningArea = remainingBounds.reduced(18);
        const auto availableWidth = juce::jmin(microphoneWarningWidth, warningArea.getWidth());

        microphoneWarning->setBounds(
            warningArea.removeFromTop(microphoneWarningHeight).removeFromRight(availableWidth));
    }
}

void MainComponent::showToolsMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addItem(tunerMenuItemId, "Tuner", true, tunerWindow != nullptr);
    menu.addItem(spectrogramMenuItemId, "Spectrogram", true, spectrogramWindow != nullptr);

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&toolsButton)
                           .withMinimumWidth(toolsMenuWidth),
                       [safeThis](int selectedItemId)
                       {
                           if (safeThis == nullptr)
                           {
                               return;
                           }

                           if (selectedItemId == tunerMenuItemId)
                           {
                               safeThis->openTool(ToolType::tuner);
                           }
                           else if (selectedItemId == spectrogramMenuItemId)
                           {
                               safeThis->openTool(ToolType::spectrogram);
                           }
                       });
}

void MainComponent::showHelpMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addItem(sendFeedbackMenuItemId, "Send feedback");

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&helpButton).withMinimumWidth(helpMenuWidth),
        [safeThis](int selectedItemId)
        {
            if (safeThis != nullptr && selectedItemId == sendFeedbackMenuItemId)
                safeThis->showFeedback();
        });
}

void MainComponent::showFeedback()
{
    if (feedbackWindow != nullptr)
    {
        feedbackWindow->setVisible(true);
        feedbackWindow->toFront(true);
        return;
    }

    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    feedbackWindow = std::make_unique<FeedbackWindow>(*settingsFile,
                                                      [safeThis]
                                                      {
                                                          if (safeThis != nullptr)
                                                              safeThis->closeFeedback();
                                                      });
    feedbackWindow->setLookAndFeel(&appLookAndFeel);
    feedbackWindow->toFront(true);
}

void MainComponent::closeFeedback()
{
    feedbackWindow.reset();
}

void MainComponent::openTool(ToolType tool)
{
    currentTool = tool;
    auto& window = windowFor(tool);

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
        {
            safeThis->closeTool(tool);
        }
    };

    window = std::make_unique<ToolWindow>(toolName(tool), createToolComponent(tool),
                                          preferredToolWindowSize(tool), closeHandler);

    const auto savedBounds = tool == ToolType::tuner ? savedTunerBounds : savedSpectrogramBounds;
    if (!savedBounds.isEmpty())
        window->setBounds(savedBounds);

    const auto palette = appPaletteFor(currentTheme);
    window->applyAppearance(&appLookAndFeel, palette.background, currentTheme);
    window->toFront(true);
}

void MainComponent::closeTool(ToolType tool)
{
    windowFor(tool).reset();
}

std::unique_ptr<juce::Component> MainComponent::createToolComponent(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        auto tuner = std::make_unique<TunerComponent>(audioInputService);
        tuner->applySettings(savedTunerSettings);
        tuner->setTheme(currentTheme);
        return tuner;
    }

    auto spectrogram = std::make_unique<SpectrogramComponent>(audioInputService);
    spectrogram->setTheme(currentTheme);
    return spectrogram;
}

juce::String MainComponent::toolName(ToolType tool) const
{
    return tool == ToolType::tuner ? "Tuner" : "Spectrogram";
}

juce::Point<int> MainComponent::preferredToolWindowSize(ToolType tool) const
{
    return tool == ToolType::tuner ? juce::Point<int>{920, 760} : juce::Point<int>{980, 650};
}

std::unique_ptr<MainComponent::ToolWindow>& MainComponent::windowFor(ToolType tool)
{
    return tool == ToolType::tuner ? tunerWindow : spectrogramWindow;
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
        audioInputService.deviceManager(), currentTheme,
        [safeThis](Theme theme)
        {
            if (safeThis != nullptr)
            {
                safeThis->setTheme(theme);
            }
        },
        [safeThis](AppDefaults::Preset preset)
        {
            if (safeThis != nullptr)
                safeThis->applyPreset(preset);
        },
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->saveSettings();
        },
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->showFeedback();
        },
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->resetCurrentTool();
        },
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->resetAudio();
        },
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->resetLayout();
        },
        [safeThis]
        {
            if (safeThis != nullptr)
                safeThis->resetAll();
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->closeSettings();
            }
        });

    const auto palette = appPaletteFor(currentTheme);
    settingsWindow->applyAppearance(&appLookAndFeel, palette.background, currentTheme);
    if (!savedSettingsBounds.isEmpty())
        settingsWindow->setBounds(savedSettingsBounds);
    settingsWindow->toFront(true);
}

void MainComponent::resetCurrentTool()
{
    auto& window = windowFor(currentTool);
    if (window == nullptr)
        return;

    if (auto* tuner = dynamic_cast<TunerComponent*>(window->getContentComponent()))
        tuner->resetToDefaults();
    else if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(window->getContentComponent()))
        spectrogram->resetToDefaults();
}

void MainComponent::resetAudio()
{
    audioInputService.resetToDefaultInput();
    updateMicrophoneWarning();
}

void MainComponent::resetLayout()
{
    if (tunerWindow != nullptr)
        tunerWindow->centreWithSize(920, 760);
    if (spectrogramWindow != nullptr)
        spectrogramWindow->centreWithSize(980, 650);
    if (settingsWindow != nullptr)
        settingsWindow->centreWithSize(760, 650);
}

void MainComponent::resetAll()
{
    setTheme(AppDefaults::theme);
    resetAudio();

    if (tunerWindow != nullptr)
        if (auto* tuner = dynamic_cast<TunerComponent*>(tunerWindow->getContentComponent()))
            tuner->resetToDefaults();

    if (spectrogramWindow != nullptr)
        if (auto* spectrogram =
                dynamic_cast<SpectrogramComponent*>(spectrogramWindow->getContentComponent()))
            spectrogram->resetToDefaults();

    resetLayout();
}

void MainComponent::applyPreset(AppDefaults::Preset preset)
{
    if (tunerWindow == nullptr)
        openTool(ToolType::tuner);

    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerWindow->getContentComponent()))
        tuner->applyPreset(preset);
}

void MainComponent::saveSettings()
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    if (tunerWindow != nullptr)
        if (auto* tuner = dynamic_cast<TunerComponent*>(tunerWindow->getContentComponent()))
            savedTunerSettings = tuner->settings();

    settingsFile->setValue(settingsSchemaKey, AppDefaults::schemaVersion);
    settingsFile->setValue(themeKey, static_cast<int>(currentTheme));
    settingsFile->setValue(tunerEasingKey, savedTunerSettings.easing);
    settingsFile->setValue(tunerAveragingKey, savedTunerSettings.averaging);
    settingsFile->setValue(tunerThresholdKey, savedTunerSettings.noteSwitchSemitones);
    settingsFile->setValue(tunerDropoutKey, savedTunerSettings.dropoutFrames);
    settingsFile->setValue(tunerDurationKey, savedTunerSettings.graphDurationSeconds);

    if (const auto audioState = audioInputService.createDeviceState())
        settingsFile->setValue(audioStateKey, audioState->toString());

    if (tunerWindow != nullptr)
        settingsFile->setValue(tunerBoundsKey, tunerWindow->getBounds().toString());
    if (spectrogramWindow != nullptr)
        settingsFile->setValue(spectrogramBoundsKey, spectrogramWindow->getBounds().toString());
    if (settingsWindow != nullptr)
        settingsFile->setValue(settingsBoundsKey, settingsWindow->getBounds().toString());

    settingsFile->saveIfNeeded();
}

void MainComponent::loadSettings()
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr ||
        settingsFile->getIntValue(settingsSchemaKey, 0) > AppDefaults::schemaVersion)
        return;

    currentTheme = static_cast<Theme>(
        settingsFile->getIntValue(themeKey, static_cast<int>(AppDefaults::theme)));
    savedTunerSettings = {
        settingsFile->getDoubleValue(tunerEasingKey, AppDefaults::Tuner::easing),
        settingsFile->getDoubleValue(tunerAveragingKey, AppDefaults::Tuner::averaging),
        settingsFile->getDoubleValue(tunerThresholdKey, AppDefaults::Tuner::noteSwitchSemitones),
        settingsFile->getDoubleValue(tunerDropoutKey, AppDefaults::Tuner::dropoutFrames),
        settingsFile->getDoubleValue(tunerDurationKey, AppDefaults::Tuner::graphDurationSeconds)};

    if (const auto xml = juce::parseXML(settingsFile->getValue(audioStateKey)); xml != nullptr)
        audioInputService.applySavedDeviceState(*xml);

    savedTunerBounds = juce::Rectangle<int>::fromString(settingsFile->getValue(tunerBoundsKey));
    savedSpectrogramBounds =
        juce::Rectangle<int>::fromString(settingsFile->getValue(spectrogramBoundsKey));
    savedSettingsBounds =
        juce::Rectangle<int>::fromString(settingsFile->getValue(settingsBoundsKey));
}

void MainComponent::closeSettings()
{
    settingsWindow.reset();
    updateMicrophoneWarning();
}

void MainComponent::setTheme(Theme theme)
{
    if (currentTheme == theme)
    {
        return;
    }

    currentTheme = theme;
    applyAppearance();
}

void MainComponent::applyAppearance()
{
    configureLookAndFeelColours();
    applyAppearanceToTopButtons();
    applyAppearanceToOpenWindows();

    sendLookAndFeelChange();
    repaint();
}

void MainComponent::configureLookAndFeelColours()
{
    const auto palette = appPaletteFor(currentTheme);

    appLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, palette.background);
    appLookAndFeel.setColour(juce::DocumentWindow::textColourId, palette.foreground);

    appLookAndFeel.setColour(juce::Label::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    appLookAndFeel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);

    appLookAndFeel.setColour(juce::TextButton::buttonColourId, palette.button);
    appLookAndFeel.setColour(juce::TextButton::buttonOnColourId, palette.buttonHover);
    appLookAndFeel.setColour(juce::TextButton::textColourOffId, palette.foreground);
    appLookAndFeel.setColour(juce::TextButton::textColourOnId, palette.foreground);

    appLookAndFeel.setColour(juce::ComboBox::backgroundColourId, palette.button);
    appLookAndFeel.setColour(juce::ComboBox::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::ComboBox::outlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::ComboBox::arrowColourId, palette.foreground);

    appLookAndFeel.setColour(juce::PopupMenu::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::PopupMenu::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::PopupMenu::headerTextColourId, palette.muted);
    appLookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId,
                             palette.accent.withAlpha(0.7f));
    appLookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, palette.foreground);

    appLookAndFeel.setColour(juce::Slider::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::Slider::trackColourId, palette.accent.withAlpha(0.75f));
    appLookAndFeel.setColour(juce::Slider::thumbColourId, palette.accent);
    appLookAndFeel.setColour(juce::Slider::textBoxTextColourId, palette.foreground);
    appLookAndFeel.setColour(juce::Slider::textBoxBackgroundColourId, palette.button);
    appLookAndFeel.setColour(juce::Slider::textBoxOutlineColourId, palette.outline);

    appLookAndFeel.setColour(juce::ToggleButton::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::ToggleButton::tickColourId, palette.accent);
    appLookAndFeel.setColour(juce::ToggleButton::tickDisabledColourId, palette.muted);

    setLookAndFeel(&appLookAndFeel);
}

void MainComponent::applyAppearanceToTopButtons()
{
    const auto palette = appPaletteFor(currentTheme);

    for (auto* button : {&fileButton, &settingsButton, &toolsButton, &helpButton})
    {
        button->setColour(juce::TextButton::buttonColourId, palette.button);
        button->setColour(juce::TextButton::buttonOnColourId, palette.buttonHover);
        button->setColour(juce::TextButton::textColourOffId, palette.foreground);
        button->setColour(juce::TextButton::textColourOnId, palette.foreground);
    }
}

void MainComponent::applyAppearanceToOpenWindows()
{
    const auto palette = appPaletteFor(currentTheme);

    if (microphoneWarning != nullptr)
    {
        microphoneWarning->setTheme(currentTheme);
    }

    if (tunerWindow != nullptr)
    {
        tunerWindow->applyAppearance(&appLookAndFeel, palette.background, currentTheme);
    }

    if (spectrogramWindow != nullptr)
    {
        spectrogramWindow->applyAppearance(&appLookAndFeel, palette.background, currentTheme);
    }

    if (settingsWindow != nullptr)
    {
        settingsWindow->applyAppearance(&appLookAndFeel, palette.background, currentTheme);
    }

    if (feedbackWindow != nullptr)
    {
        feedbackWindow->setLookAndFeel(&appLookAndFeel);
        feedbackWindow->sendLookAndFeelChange();
        feedbackWindow->repaint();
    }
}

bool MainComponent::hasUsableMicrophone() const
{
    return audioInputService.hasUsableInput();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioInputService)
        updateMicrophoneWarning();
}

void MainComponent::updateMicrophoneWarning()
{
    if (microphoneWarning == nullptr)
    {
        return;
    }

    if (hasUsableMicrophone())
    {
        // Reset the dismissal after a working microphone appears. If that
        // device later disappears, the warning is useful again.
        isMicrophoneWarningDismissed = false;
        microphoneWarning->setVisible(false);
        return;
    }

    microphoneWarning->setVisible(!isMicrophoneWarningDismissed);

    if (microphoneWarning->isVisible())
    {
        microphoneWarning->toFront(false);
    }
}

void MainComponent::dismissMicrophoneWarning()
{
    isMicrophoneWarningDismissed = true;

    if (microphoneWarning != nullptr)
    {
        microphoneWarning->setVisible(false);
    }
}
