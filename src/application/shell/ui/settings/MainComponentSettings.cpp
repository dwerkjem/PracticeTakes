#include "../../MainComponent.h"

#include "../../../../features/analysis/harmonics/HarmonicAnalyzerComponent.h"
#include "../../../../features/analysis/spectrogram/SpectrogramComponent.h"
#include "../../../../features/analysis/tuner/TunerComponent.h"
#include "../workspace/components/ToolWindow.h"
#include "SettingsWindow.h"

namespace
{
constexpr int openSettingsMenuItemId = 1;
constexpr int lightSettingsMenuItemId = 2;
constexpr int darkSettingsMenuItemId = 3;
constexpr int muteSettingsMenuItemId = 4;
constexpr int normalFullscreenMenuItemId = 5;
constexpr int kioskFullscreenMenuItemId = 6;

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

void MainComponent::showSettingsMenu()
{
    juce::PopupMenu appearanceMenu;
    appearanceMenu.addItem(
        lightSettingsMenuItemId, "Light theme", true, currentTheme == Theme::light);
    appearanceMenu.addItem(darkSettingsMenuItemId, "Dark theme", true, currentTheme == Theme::dark);

    juce::PopupMenu fullscreenMenu;
    fullscreenMenu.addItem(
        normalFullscreenMenuItemId, "Normal fullscreen", true,
        selectedFullscreenMode == AppSettings::FullscreenMode::normal);
    fullscreenMenu.addItem(
        kioskFullscreenMenuItemId, "Kiosk fullscreen", true,
        selectedFullscreenMode == AppSettings::FullscreenMode::kiosk);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addSubMenu("Appearance", appearanceMenu);
    menu.addSubMenu("Fullscreen mode", fullscreenMenu);
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
            {
                return;
            }

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
            case normalFullscreenMenuItemId:
                safeThis->selectedFullscreenMode = AppSettings::FullscreenMode::normal;
                safeThis->saveSettings();
                break;
            case kioskFullscreenMenuItemId:
                safeThis->selectedFullscreenMode = AppSettings::FullscreenMode::kiosk;
                safeThis->saveSettings();
                break;
            default:
                break;
            }
        });
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
            {
                safeThis->applyPreset(preset);
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->saveSettings(true);
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->showFeedback();
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->resetCurrentTool();
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->resetAudio();
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->resetLayout();
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->resetAll();
            }
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
    {
        settingsWindow->setBounds(savedSettingsBounds);
    }
    settingsWindow->toFront(true);
}

void MainComponent::resetCurrentTool()
{
    auto& component = componentFor(currentTool);
    if (component == nullptr)
    {
        return;
    }

    if (auto* tuner = dynamic_cast<TunerComponent*>(component.get()))
    {
        tuner->resetToDefaults();
    }
    else if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(component.get()))
    {
        spectrogram->resetToDefaults();
    }
    else if (auto* harmonics = dynamic_cast<HarmonicAnalyzerComponent*>(component.get()))
    {
        harmonics->resetToDefaults();
    }
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
    {
        tunerWindow->centreWithSize(920, 760);
    }
    if (spectrogramState.presentation() == WorkspaceToolState::Presentation::floating)
    {
        spectrogramWindow->centreWithSize(980, 650);
    }
    if (harmonicState.presentation() == WorkspaceToolState::Presentation::floating)
    {
        harmonicWindow->centreWithSize(980, 700);
    }
    if (settingsWindow != nullptr)
    {
        settingsWindow->centreWithSize(900, 760);
    }
}

void MainComponent::resetAll()
{
    setTheme(AppDefaults::theme);
    selectedFullscreenMode = AppSettings::FullscreenMode::normal;
    resetAudio();
    savedTunerSettings = AppDefaults::tunerDefaults();
    currentTool = ToolType::tuner;

    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerComponent.get()))
    {
        tuner->resetToDefaults();
    }
    if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(spectrogramComponent.get()))
    {
        spectrogram->resetToDefaults();
    }
    if (auto* harmonics = dynamic_cast<HarmonicAnalyzerComponent*>(harmonicComponent.get()))
    {
        harmonics->resetToDefaults();
    }
    resetLayout();
}

void MainComponent::applyPreset(AppDefaults::Preset preset)
{
    if (!tunerState.isOpen())
    {
        openTool(ToolType::tuner);
    }
    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerComponent.get()))
    {
        tuner->applyPreset(preset);
    }
}

AppSettings::State MainComponent::captureSettingsState()
{
    if (tunerWindow != nullptr)
    {
        savedTunerBounds = tunerWindow->getBounds();
    }
    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerComponent.get()))
    {
        savedTunerSettings = tuner->settings();
    }
    if (spectrogramWindow != nullptr)
    {
        savedSpectrogramBounds = spectrogramWindow->getBounds();
    }
    if (harmonicWindow != nullptr)
    {
        savedHarmonicBounds = harmonicWindow->getBounds();
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
    state.harmonicBounds = savedHarmonicBounds.toString();
    state.settingsBounds = savedSettingsBounds.toString();
    if (currentTool == ToolType::tuner)
    {
        state.recentTool = AppSettings::RecentTool::tuner;
    }
    else if (currentTool == ToolType::spectrogram)
    {
        state.recentTool = AppSettings::RecentTool::spectrogram;
    }
    else
    {
        state.recentTool = AppSettings::RecentTool::harmonics;
    }
    state.fullscreenMode = selectedFullscreenMode;
    if (pendingAudioDeviceState != nullptr)
    {
        state.audioDeviceState = pendingAudioDeviceState->toString();
    }
    else if (const auto audioState = audioInputService.createDeviceState())
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
    if (loaded.state.recentTool == AppSettings::RecentTool::spectrogram)
    {
        currentTool = ToolType::spectrogram;
    }
    else if (loaded.state.recentTool == AppSettings::RecentTool::harmonics)
    {
        currentTool = ToolType::harmonics;
    }
    else
    {
        currentTool = ToolType::tuner;
    }
    selectedFullscreenMode = loaded.state.fullscreenMode;

    if (auto xml = juce::parseXML(loaded.state.audioDeviceState); xml != nullptr)
    {
        pendingAudioDeviceState = std::move(xml);
    }

    savedTunerBounds = validWindowBounds(loaded.state.tunerBounds);
    savedSpectrogramBounds = validWindowBounds(loaded.state.spectrogramBounds);
    savedHarmonicBounds = validWindowBounds(loaded.state.harmonicBounds);
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
