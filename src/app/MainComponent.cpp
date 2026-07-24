#include "MainComponent.h"

#include "../feedback/FeedbackInvitationPolicy.h"
#include "../spectrogram/SpectrogramComponent.h"
#include "../tuner/TunerComponent.h"

#include "AppWindows.h"
#include "MainTitleBar.h"
#include "MicrophoneWarning.h"

namespace
{
constexpr int toolsMenuWidth = 190;
constexpr int helpMenuWidth = 190;

constexpr int microphoneWarningWidth = 470;
constexpr int microphoneWarningHeight = 118;

constexpr int tunerMenuItemId = 1;
constexpr int spectrogramMenuItemId = 2;
constexpr int sendFeedbackMenuItemId = 1;
constexpr int feedbackInvitationsMenuItemId = 2;
constexpr int openSettingsMenuItemId = 1;
constexpr int lightSettingsMenuItemId = 2;
constexpr int darkSettingsMenuItemId = 3;
constexpr int muteSettingsMenuItemId = 4;
constexpr auto settingsSchemaKey = "settings.schema";
constexpr auto themeKey = "global.theme";
constexpr auto audioStateKey = "audio.deviceState";
constexpr auto audioInputGainKey = "audio.inputGain";
constexpr auto tunerEasingKey = "tuner.easing";
constexpr auto tunerAveragingKey = "tuner.averaging";
constexpr auto tunerThresholdKey = "tuner.noteSwitch";
constexpr auto tunerDropoutKey = "tuner.dropout";
constexpr auto tunerDurationKey = "tuner.graphDuration";
constexpr auto tunerBoundsKey = "layout.tuner";
constexpr auto spectrogramBoundsKey = "layout.spectrogram";
constexpr auto settingsBoundsKey = "layout.settings";
constexpr auto feedbackSuccessfulUsesKey = "feedback.successfulToolUses";
constexpr auto feedbackInvitationShownKey = "feedback.invitationShown";
constexpr auto feedbackInvitationsDisabledKey = "feedback.invitationsDisabled";

} // namespace

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
    updateMicrophoneStateControl();
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
    // File is intentionally present but inactive while the project/file model
    // is still being designed.
    fileButton.setTooltip("File actions will be added later.");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    toolsButton.onClick = [this] { showToolsMenu(); };
    helpButton.onClick = [this] { showHelpMenu(); };
    microphoneButton.setTitle("Global microphone mute control");
    microphoneButton.onClick = [this] { audioInputService.toggleMuted(); };
}

std::unique_ptr<MainTitleBar> MainComponent::createTitleBar(
    const juce::String& title,
    std::function<void()> minimiseHandler,
    std::function<void()> fullscreenHandler,
    std::function<void()> closeHandler)
{
    return std::make_unique<MainTitleBar>(
        title, fileButton, settingsButton, toolsButton, helpButton, microphoneButton,
        std::move(minimiseHandler), std::move(fullscreenHandler), std::move(closeHandler));
}

void MainComponent::createMicrophoneWarning()
{
    microphoneWarning = std::make_unique<MicrophoneWarning>(
        [this] { showSettings(); }, [this] { dismissMicrophoneWarning(); });

    // Start hidden; updateMicrophoneWarning decides whether it is needed.
    addChildComponent(*microphoneWarning);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    const auto palette = appPaletteFor(currentTheme);
    graphics.fillAll(palette.background);
}

void MainComponent::resized()
{
    auto remainingBounds = getLocalBounds();

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
    menu.showMenuAsync(
        juce::PopupMenu::Options()
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

void MainComponent::showSettingsMenu()
{
    juce::PopupMenu appearanceMenu;
    appearanceMenu.addItem(
        lightSettingsMenuItemId, "Light theme", true, currentTheme == Theme::light);
    appearanceMenu.addItem(darkSettingsMenuItemId, "Dark theme", true, currentTheme == Theme::dark);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addSubMenu("Appearance", appearanceMenu);
    menu.addItem(
        muteSettingsMenuItemId,
        audioInputService.isMuted() ? "Unmute microphone" : "Mute microphone");
    menu.addSeparator();
    menu.addItem(openSettingsMenuItemId, "Open full settings...");

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&settingsButton).withMinimumWidth(230),
        [safeThis](int selectedItemId)
        {
            if (safeThis == nullptr)
                return;

            switch (selectedItemId)
            {
            case openSettingsMenuItemId:
                safeThis->showSettings();
                break;
            case lightSettingsMenuItemId:
                safeThis->setTheme(Theme::light);
                break;
            case darkSettingsMenuItemId:
                safeThis->setTheme(Theme::dark);
                break;
            case muteSettingsMenuItemId:
                safeThis->audioInputService.toggleMuted();
                break;
            default:
                break;
            }
        });
}

void MainComponent::showHelpMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addItem(sendFeedbackMenuItemId, "Send feedback");
    menu.addSeparator();
    menu.addItem(
        feedbackInvitationsMenuItemId,
        feedbackInvitationsDisabled() ? "Enable feedback invitations"
                                      : "Disable feedback invitations");

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&helpButton).withMinimumWidth(helpMenuWidth),
        [safeThis](int selectedItemId)
        {
            if (safeThis == nullptr)
                return;
            if (selectedItemId == sendFeedbackMenuItemId)
                safeThis->showFeedback();
            else if (selectedItemId == feedbackInvitationsMenuItemId)
                safeThis->setFeedbackInvitationsDisabled(!safeThis->feedbackInvitationsDisabled());
        });
}

void MainComponent::showFeedback(const juce::String& context)
{
    if (feedbackWindow != nullptr)
    {
        if (context.isNotEmpty())
            feedbackWindow->setContextTag(context);
        feedbackWindow->setVisible(true);
        feedbackWindow->toFront(true);
        return;
    }

    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    feedbackWindow = std::make_unique<FeedbackWindow>(
        *settingsFile, context,
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

    window = std::make_unique<ToolWindow>(
        toolName(tool), createToolComponent(tool), preferredToolWindowSize(tool), closeHandler);

    const auto savedBounds = tool == ToolType::tuner ? savedTunerBounds : savedSpectrogramBounds;
    if (!savedBounds.isEmpty())
        window->setBounds(savedBounds);

    const auto palette = appPaletteFor(currentTheme);
    window->applyAppearance(&appLookAndFeel, palette.background, currentTheme);
    window->toFront(true);
}

void MainComponent::closeTool(ToolType tool)
{
    auto& window = windowFor(tool);
    const auto wasOpen = window != nullptr;
    window.reset();
    if (wasOpen)
        recordSuccessfulToolUse();
}

std::unique_ptr<juce::Component> MainComponent::createToolComponent(ToolType tool)
{
    if (tool == ToolType::tuner)
    {
        auto tuner = std::make_unique<TunerComponent>(
            audioInputService, [this] { showFeedback(toolName(ToolType::tuner)); });
        tuner->applySettings(savedTunerSettings);
        tuner->setTheme(currentTheme);
        return tuner;
    }

    auto spectrogram = std::make_unique<SpectrogramComponent>(
        audioInputService, [this] { showFeedback(toolName(ToolType::spectrogram)); });
    spectrogram->setTheme(currentTheme);
    return spectrogram;
}

void MainComponent::recordSuccessfulToolUse()
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    const auto uses = settingsFile->getIntValue(feedbackSuccessfulUsesKey, 0) + 1;
    settingsFile->setValue(feedbackSuccessfulUsesKey, uses);
    settingsFile->saveIfNeeded();
    maybeOfferFeedbackInvitation();
}

void MainComponent::maybeOfferFeedbackInvitation()
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
        return;

    const FeedbackInvitationPolicy::State state{
        settingsFile->getIntValue(feedbackSuccessfulUsesKey, 0),
        settingsFile->getBoolValue(feedbackInvitationShownKey, false),
        settingsFile->getBoolValue(feedbackInvitationsDisabledKey, false)};
    const auto isLiveSessionActive = tunerWindow != nullptr || spectrogramWindow != nullptr;
    if (!FeedbackInvitationPolicy::shouldInvite(state, isLiveSessionActive))
        return;

    // Mark the one-time invitation before displaying it so closing the dialog
    // is also respected and never causes a repeated interruption.
    settingsFile->setValue(feedbackInvitationShownKey, true);
    settingsFile->saveIfNeeded();

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showYesNoCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "Help improve Practice Takes",
        "Would you like to share feedback about the tools you have tried?", "Give feedback",
        "Not now", "Never ask again", nullptr,
        juce::ModalCallbackFunction::create(
            [safeThis](int result)
            {
                if (safeThis == nullptr)
                    return;
                if (result == 1)
                    safeThis->showFeedback("Early tester experience");
                else if (result == 0)
                    safeThis->setFeedbackInvitationsDisabled(true);
            }));
}

void MainComponent::setFeedbackInvitationsDisabled(bool disabled)
{
    if (auto* settingsFile = applicationProperties.getUserSettings())
    {
        settingsFile->setValue(feedbackInvitationsDisabledKey, disabled);
        if (!disabled)
            settingsFile->setValue(feedbackInvitationShownKey, false);
        settingsFile->saveIfNeeded();
    }
}

bool MainComponent::feedbackInvitationsDisabled()
{
    if (auto* settingsFile = applicationProperties.getUserSettings())
        return settingsFile->getBoolValue(feedbackInvitationsDisabledKey, false);
    return false;
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
        audioInputService, currentTheme,
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
        settingsWindow->centreWithSize(900, 760);
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
    settingsFile->setValue(audioInputGainKey, audioInputService.inputGain());
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
    audioInputService.setInputGain(
        static_cast<float>(
            settingsFile->getDoubleValue(audioInputGainKey, AppDefaults::Audio::inputGain)));
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
    appLookAndFeel.setColour(
        juce::PopupMenu::highlightedBackgroundColourId, palette.accent.withAlpha(0.7f));
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

    updateMicrophoneStateControl();
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
    {
        updateMicrophoneStateControl();
        updateMicrophoneWarning();
    }
}

void MainComponent::updateMicrophoneStateControl()
{
    const auto palette = appPaletteFor(currentTheme);
    auto colour = palette.accent;
    juce::String text;
    juce::String tooltip;

    switch (audioInputService.inputState())
    {
    case AudioInputService::InputState::disconnected:
        text = "Mic disconnected";
        tooltip = "No microphone is available; open Settings to choose an input";
        colour = palette.muted;
        break;
    case AudioInputService::InputState::muted:
        text = "Mic muted - Unmute";
        tooltip = "Resume audio analysis using the selected microphone";
        colour = palette.warning;
        break;
    case AudioInputService::InputState::clipping:
        text = "Mic clipping - Mute";
        tooltip = "The microphone level is clipping; click to mute all analysis tools";
        colour = juce::Colours::red;
        break;
    case AudioInputService::InputState::active:
        text = "Mic active - Mute";
        tooltip = "Mute the microphone for every analysis tool";
        break;
    }

    microphoneButton.setButtonText(text);
    microphoneButton.setTooltip(tooltip);
    microphoneButton.setColour(juce::TextButton::buttonColourId, colour.withAlpha(0.75f));
    microphoneButton.setColour(juce::TextButton::buttonOnColourId, colour);
    microphoneButton.setColour(juce::TextButton::textColourOffId, palette.foreground);
    microphoneButton.setColour(juce::TextButton::textColourOnId, palette.foreground);
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
