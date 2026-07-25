#include "MainComponent.h"

#include "../feedback/FeedbackInvitationPolicy.h"
#include "../spectrogram/SpectrogramComponent.h"
#include "../tuner/TunerComponent.h"

#include "MainTitleBar.h"
#include "MicrophoneWarning.h"
#include "settings/SettingsWindow.h"
#include "support/FeedbackWindow.h"
#include "workspace/DockedToolPanel.h"
#include "workspace/ToolWindow.h"

namespace
{
constexpr int helpMenuWidth = 190;

constexpr int microphoneWarningWidth = 470;
constexpr int microphoneWarningHeight = 118;

constexpr int sendFeedbackMenuItemId = 1;
constexpr int feedbackInvitationsMenuItemId = 2;
constexpr int openSettingsMenuItemId = 1;
constexpr int lightSettingsMenuItemId = 2;
constexpr int darkSettingsMenuItemId = 3;
constexpr int muteSettingsMenuItemId = 4;
constexpr auto feedbackSuccessfulUsesKey = "feedback.successfulToolUses";
constexpr auto feedbackInvitationShownKey = "feedback.invitationShown";
constexpr auto feedbackInvitationsDisabledKey = "feedback.invitationsDisabled";

[[nodiscard]] juce::Rectangle<int> validWindowBounds(const juce::String& storedBounds)
{
    const auto bounds = juce::Rectangle<int>::fromString(storedBounds);
    constexpr int minimumWindowSize = 300;
    constexpr int maximumWindowSize = 10000;
    if (bounds.getWidth() < minimumWindowSize || bounds.getHeight() < minimumWindowSize ||
        bounds.getWidth() > maximumWindowSize || bounds.getHeight() > maximumWindowSize)
    {
        return {};
    }
    return bounds;
}
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
    // PropertiesFile writes through a temporary sibling and atomically replaces
    // the destination. Keep writes explicit so a complete snapshot is flushed
    // instead of serialising each individual field.
    storageOptions.millisecondsBeforeSaving = -1;
    storageOptions.storageFormat = juce::PropertiesFile::storeAsXML;
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
    saveSettings();
    audioInputService.removeChangeListener(this);
    // Presentations do not own their tool components. Detach them before
    // destroying the components that unregister from the shared audio service.
    settingsWindow.reset();
    feedbackWindow.reset();
    tunerDock.reset();
    spectrogramDock.reset();
    spectrogramWindow.reset();
    tunerWindow.reset();
    spectrogramComponent.reset();
    tunerComponent.reset();
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

    auto workspaceBounds = remainingBounds.reduced(18);
    const auto hasTunerDock = tunerDock != nullptr;
    const auto hasSpectrogramDock = spectrogramDock != nullptr;
    if (hasTunerDock && hasSpectrogramDock)
    {
        constexpr int gap = 10;
        const auto firstWidth = (workspaceBounds.getWidth() - gap) / 2;
        tunerDock->setBounds(workspaceBounds.removeFromLeft(firstWidth));
        workspaceBounds.removeFromLeft(gap);
        spectrogramDock->setBounds(workspaceBounds);
    }
    else if (hasTunerDock)
    {
        tunerDock->setBounds(workspaceBounds);
    }
    else if (hasSpectrogramDock)
    {
        spectrogramDock->setBounds(workspaceBounds);
    }

    if (microphoneWarning != nullptr)
    {
        auto warningArea = remainingBounds.reduced(18);
        const auto availableWidth = juce::jmin(microphoneWarningWidth, warningArea.getWidth());

        microphoneWarning->setBounds(
            warningArea.removeFromTop(microphoneWarningHeight).removeFromRight(availableWidth));
        if (microphoneWarning->isVisible())
            microphoneWarning->toFront(false);
    }
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
    const auto isLiveSessionActive = tunerState.isOpen() || spectrogramState.isOpen();
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
                safeThis->saveSettings(true);
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
    auto& component = componentFor(currentTool);
    if (component == nullptr)
        return;

    if (auto* tuner = dynamic_cast<TunerComponent*>(component.get()))
        tuner->resetToDefaults();
    else if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(component.get()))
        spectrogram->resetToDefaults();
}

void MainComponent::resetAudio()
{
    audioInputService.setMuted(false);
    audioInputService.resetToDefaultInput();
    updateMicrophoneWarning();
}

void MainComponent::resetLayout()
{
    savedTunerBounds = {};
    savedSpectrogramBounds = {};
    savedSettingsBounds = {};

    if (tunerState.presentation() == WorkspaceToolState::Presentation::floating)
        tunerWindow->centreWithSize(920, 760);
    if (spectrogramState.presentation() == WorkspaceToolState::Presentation::floating)
        spectrogramWindow->centreWithSize(980, 650);
    if (settingsWindow != nullptr)
        settingsWindow->centreWithSize(900, 760);
}

void MainComponent::resetAll()
{
    setTheme(AppDefaults::theme);
    resetAudio();
    savedTunerSettings = AppDefaults::tunerDefaults();
    currentTool = ToolType::tuner;

    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerComponent.get()))
        tuner->resetToDefaults();

    if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(spectrogramComponent.get()))
        spectrogram->resetToDefaults();

    resetLayout();
}

void MainComponent::applyPreset(AppDefaults::Preset preset)
{
    if (!tunerState.isOpen())
        openTool(ToolType::tuner);

    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerComponent.get()))
        tuner->applyPreset(preset);
}

AppSettings::State MainComponent::captureSettingsState()
{
    if (tunerWindow != nullptr)
    {
        savedTunerBounds = tunerWindow->getBounds();
    }
    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerComponent.get()))
        savedTunerSettings = tuner->settings();
    if (spectrogramWindow != nullptr)
    {
        savedSpectrogramBounds = spectrogramWindow->getBounds();
    }
    if (settingsWindow != nullptr)
    {
        savedSettingsBounds = settingsWindow->getBounds();
    }

    AppSettings::State state;
    state.theme = currentTheme;
    state.microphoneMuted = audioInputService.isMuted();
    state.inputGain = audioInputService.inputGain();
    state.tuner = savedTunerSettings;
    state.tunerBounds = savedTunerBounds.toString();
    state.spectrogramBounds = savedSpectrogramBounds.toString();
    state.settingsBounds = savedSettingsBounds.toString();
    state.recentTool = currentTool == ToolType::tuner ? AppSettings::RecentTool::tuner
                                                      : AppSettings::RecentTool::spectrogram;

    if (const auto audioState = audioInputService.createDeviceState())
    {
        state.audioDeviceState = audioState->toString();
    }
    return state;
}

void MainComponent::saveSettings(bool explicitSave)
{
    if (!automaticSettingsSaveEnabled && !explicitSave)
    {
        return;
    }

    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
    {
        return;
    }

    if (explicitSave && !automaticSettingsSaveEnabled)
    {
        AppSettings::clearOwnedValues(*settingsFile);
        automaticSettingsSaveEnabled = true;
    }

    AppSettings::store(*settingsFile, captureSettingsState());
    settingsFile->saveIfNeeded();
}

void MainComponent::loadSettings()
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
    {
        return;
    }

    const auto loaded = AppSettings::load(*settingsFile);
    automaticSettingsSaveEnabled = loaded.status != AppSettings::LoadStatus::newerSchema;
    currentTheme = loaded.state.theme;
    audioInputService.setMuted(loaded.state.microphoneMuted);
    audioInputService.setInputGain(static_cast<float>(loaded.state.inputGain));
    savedTunerSettings = loaded.state.tuner;
    currentTool = loaded.state.recentTool == AppSettings::RecentTool::spectrogram
                      ? ToolType::spectrogram
                      : ToolType::tuner;

    if (const auto xml = juce::parseXML(loaded.state.audioDeviceState); xml != nullptr)
    {
        audioInputService.applySavedDeviceState(*xml);
    }

    savedTunerBounds = validWindowBounds(loaded.state.tunerBounds);
    savedSpectrogramBounds = validWindowBounds(loaded.state.spectrogramBounds);
    savedSettingsBounds = validWindowBounds(loaded.state.settingsBounds);

    if (loaded.status == AppSettings::LoadStatus::recoveredFromCorruption)
    {
        const auto settingsPath = settingsFile->getFile();
        if (settingsPath.existsAsFile())
        {
            const auto backup = settingsPath.getSiblingFile(settingsPath.getFileName() + ".corrupt")
                                    .getNonexistentSibling();
            juce::ignoreUnused(settingsPath.moveFileTo(backup));
        }
        settingsFile->clear();
    }

    if (loaded.status == AppSettings::LoadStatus::migrated ||
        loaded.status == AppSettings::LoadStatus::recoveredFromCorruption)
    {
        AppSettings::store(*settingsFile, loaded.state);
        settingsFile->saveIfNeeded();
    }
}

void MainComponent::closeSettings()
{
    if (settingsWindow != nullptr)
    {
        savedSettingsBounds = settingsWindow->getBounds();
    }
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

    if (tunerState.isOpen())
        applyAppearanceToTool(ToolType::tuner);
    if (spectrogramState.isOpen())
        applyAppearanceToTool(ToolType::spectrogram);

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
