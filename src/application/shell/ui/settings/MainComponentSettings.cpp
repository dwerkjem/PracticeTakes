#include "../../MainComponent.h"

#include "../../../../features/analysis/tuner/TunerComponent.h"
#include "../../../../features/analysis/tuner/TunerSettingsCodec.h"
#include "../../../configuration/SettingsImportPersistence.h"
#include "../../../configuration/SettingsImportTransaction.h"
#include "../../../configuration/SettingsTransferCodec.h"
#include "../../../configuration/SettingsTransferFile.h"
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
                safeThis->importSettings();
            }
        },
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->exportSettings();
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

void MainComponent::importSettings()
{
    settingsTransferChooser = std::make_unique<juce::FileChooser>(
        "Import settings", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.ptsettings", true);
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    settingsTransferChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            std::vector<WorkspaceBounds> displayAreas;
            for (const auto& area :
                 juce::Desktop::getInstance().getDisplays().getRectangleList(true))
            {
                displayAreas.push_back(
                    {area.getX(), area.getY(), area.getWidth(), area.getHeight()});
            }
            const auto staged = SettingsImportTransaction::stage(chooser.getResult(), displayAreas);
            safeThis->settingsTransferChooser.reset();
            if (staged.status == SettingsImportStageStatus::cancelled)
            {
                return;
            }
            if (staged.status != SettingsImportStageStatus::ready || !staged.model.has_value())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, "Settings import failed",
                    juce::String::fromUTF8(staged.error.c_str()));
                return;
            }
            safeThis->confirmSettingsImport(*staged.model);
        });
}

void MainComponent::confirmSettingsImport(SettingsTransferModel model)
{
    auto activeWorkspace = juce::String{"Customized workspace"};
    if (const auto& activeSource = model.state.workspaceCatalog.activeSource;
        activeSource.has_value())
    {
        if (const auto* builtIn = WorkspaceBuiltIns::find(*activeSource))
        {
            activeWorkspace = builtIn->name;
        }
        else
        {
            for (const auto& named : model.state.workspaceCatalog.named)
            {
                if (named.id == *activeSource)
                {
                    activeWorkspace = named.name;
                    break;
                }
            }
        }
    }

    const auto summary =
        "This replaces supported preferences and all named workspaces.\n\nTheme: " +
        juce::String(model.state.theme == Theme::dark ? "Dark" : "Light") + "\nAudio: Microphone " +
        (model.state.microphoneMuted ? "muted" : "enabled") + ", input gain " +
        juce::String(model.state.inputGain, 2) + "\nActive workspace: " + activeWorkspace +
        "\nNamed workspaces: " +
        juce::String(static_cast<int>(model.state.workspaceCatalog.named.size()));
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "Replace settings?", summary, "Import", "Cancel",
        this,
        juce::ModalCallbackFunction::create(
            [safeThis, model = std::move(model)](int response)
            {
                if (safeThis != nullptr && response != 0)
                {
                    safeThis->executeSettingsImport(model);
                }
            }));
}

void MainComponent::executeSettingsImport(const SettingsTransferModel& model)
{
    auto* settingsFile = applicationProperties.getUserSettings();
    if (settingsFile == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Settings import failed",
            "The application settings file is unavailable.");
        return;
    }

    const auto result = SettingsImportTransaction::execute(
        model, true, automaticSettingsSaveEnabled,
        {[this]
         {
             SettingsTransferModel prior;
             prior.applicationVersion =
                 juce::JUCEApplication::getInstance()->getApplicationVersion().toStdString();
             prior.state = captureSettingsState();
             prior.feedbackInvitationsDisabled = feedbackInvitationsDisabledState;
             return prior;
         },
         [this](const SettingsTransferModel& state) { return applyTransferredSettings(state); },
         [this, settingsFile](const SettingsTransferModel& state)
         {
             return SettingsImportPersistence::persist(
                 *settingsFile, applicationProperties.getStorageParameters(), state);
         }});

    showSettingsImportResult(result);
}

void MainComponent::showSettingsImportResult(const SettingsImportResult& result)
{
    juce::AlertWindow::showMessageBoxAsync(
        result.status == SettingsImportStatus::imported ? juce::MessageBoxIconType::InfoIcon
                                                        : juce::MessageBoxIconType::WarningIcon,
        result.status == SettingsImportStatus::imported ? "Settings imported"
                                                        : "Settings import failed",
        result.status == SettingsImportStatus::imported
            ? "Your settings and workspaces were imported successfully."
            : juce::String::fromUTF8(result.error.c_str()));
}

void MainComponent::exportSettings()
{
    SettingsTransferModel model;
    model.applicationVersion =
        juce::JUCEApplication::getInstance()->getApplicationVersion().toStdString();
    model.state = captureSettingsState();
    model.feedbackInvitationsDisabled = feedbackInvitationsDisabled();

    const auto encoded = SettingsTransferCodec::encode(model);
    if (encoded.status != SettingsTransferEncodeStatus::encoded || !encoded.document.has_value())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Settings export failed",
            juce::String::fromUTF8(encoded.error.c_str()));
        return;
    }

    settingsTransferChooser = std::make_unique<juce::FileChooser>(
        "Export settings",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("practice-takes.ptsettings"),
        "*.ptsettings", true);
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    settingsTransferChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, document = *encoded.document](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            const auto result = SettingsTransferFile::exportDocument(document, chooser.getResult());
            safeThis->settingsTransferChooser.reset();
            if (result.status == SettingsTransferExportStatus::cancelled)
            {
                return;
            }

            juce::AlertWindow::showMessageBoxAsync(
                result.status == SettingsTransferExportStatus::exported
                    ? juce::MessageBoxIconType::InfoIcon
                    : juce::MessageBoxIconType::WarningIcon,
                result.status == SettingsTransferExportStatus::exported
                    ? "Settings exported"
                    : "Settings export failed",
                result.status == SettingsTransferExportStatus::exported
                    ? "Your portable settings were exported successfully."
                    : juce::String::fromUTF8(result.error.c_str()));
        });
}

namespace
{
// The .ptsettings schema predates the tool registry and names three tools in
// fixed fields. Rather than migrate that format, these two functions are the
// single adapter between it and the id-keyed runtime -- the only place in the
// shell that still spells a tool id out.
constexpr auto tunerToolId = "tuner";
constexpr auto spectrogramToolId = "spectrogram";
constexpr auto harmonicToolId = "harmonic-analyzer";
constexpr auto defaultToolId = tunerToolId;
} // namespace

void MainComponent::applyLegacyToolState(const AppSettings::State& state)
{
    const auto restore = [this](std::string_view toolId, const juce::String& bounds)
    {
        if (auto* entry = ensureLiveTool(instanceIdToOpen(toolId)))
        {
            entry->savedBounds = validWindowBounds(bounds);
        }
    };
    restore(tunerToolId, state.tunerBounds);
    restore(spectrogramToolId, state.spectrogramBounds);
    restore(harmonicToolId, state.harmonicBounds);

    if (auto* tuner = ensureLiveTool(instanceIdToOpen(tunerToolId)))
    {
        tuner->savedSettings = TunerSettingsCodec::encode(state.tuner);
    }

    switch (state.recentTool)
    {
    case AppSettings::RecentTool::spectrogram:
        currentTool = instanceIdToOpen(spectrogramToolId);
        break;
    case AppSettings::RecentTool::harmonics:
        currentTool = instanceIdToOpen(harmonicToolId);
        break;
    default:
        currentTool = instanceIdToOpen(tunerToolId);
        break;
    }
}

void MainComponent::captureLegacyToolState(AppSettings::State& state)
{
    const auto boundsOf = [this](std::string_view toolId)
    {
        const auto* entry = findLiveTool(instanceIdToOpen(toolId));
        return entry != nullptr ? entry->savedBounds : juce::Rectangle<int>{};
    };
    state.tunerBounds = boundsOf(tunerToolId).toString();
    state.spectrogramBounds = boundsOf(spectrogramToolId).toString();
    state.harmonicBounds = boundsOf(harmonicToolId).toString();

    if (const auto* tuner = findLiveTool(instanceIdToOpen(tunerToolId));
        tuner != nullptr && tuner->savedSettings.has_value())
    {
        state.tuner =
            TunerSettingsCodec::decode(tuner->savedSettings).value_or(AppDefaults::tunerDefaults());
    }
    else
    {
        state.tuner = AppDefaults::tunerDefaults();
    }

    const auto focused = currentTool.toolId();
    if (focused == spectrogramToolId)
    {
        state.recentTool = AppSettings::RecentTool::spectrogram;
    }
    else if (focused == harmonicToolId)
    {
        state.recentTool = AppSettings::RecentTool::harmonics;
    }
    else
    {
        state.recentTool = AppSettings::RecentTool::tuner;
    }
}

bool MainComponent::applyTransferredSettings(const SettingsTransferModel& model)
{
    const auto& state = model.state;
    setTheme(state.theme);
    selectedFullscreenMode = state.fullscreenMode;
    feedbackInvitationsDisabledState = model.feedbackInvitationsDisabled;
    audioInputService.setMuted(state.microphoneMuted);
    audioInputService.setInputGain(static_cast<float>(state.inputGain));
    applyLegacyToolState(state);
    savedSettingsBounds = validWindowBounds(state.settingsBounds);
    workspaceCatalog = state.workspaceCatalog;

    if (auto audioState = juce::parseXML(state.audioDeviceState); audioState != nullptr)
    {
        audioInputService.applySavedDeviceState(*audioState);
    }
    else
    {
        audioInputService.resetToDefaultInput();
    }
    updateMicrophoneStateControl();
    updateMicrophoneWarning();

    return applyWorkspaceSnapshot(workspaceCatalog.active);
}

void MainComponent::resetCurrentTool()
{
    auto* entry = findLiveTool(currentTool);
    if (entry == nullptr || entry->component == nullptr)
    {
        return;
    }

    entry->component->resetToDefaults();
    entry->savedSettings.reset();
}

void MainComponent::resetAudio()
{
    audioInputService.setMuted(false);
    audioInputService.resetToDefaultInput();
    updateMicrophoneWarning();
}

void MainComponent::resetLayout()
{
    WorkspaceSessionLifecycle::resetActive(workspaceCatalog);
    static_cast<void>(applyWorkspaceSnapshot(workspaceCatalog.active));
    saveSettings();
}

void MainComponent::resetAll()
{
    setTheme(AppDefaults::theme);
    selectedFullscreenMode = AppSettings::FullscreenMode::normal;
    resetAudio();
    currentTool = instanceIdToOpen(defaultToolId);

    for (auto& entry : liveTools)
    {
        entry.savedSettings.reset();
        if (entry.component != nullptr)
        {
            entry.component->resetToDefaults();
        }
    }
    resetLayout();
}

void MainComponent::applyPreset(AppDefaults::Preset preset)
{
    const auto tuner = instanceIdToOpen(tunerToolId);
    if (!toolIsOpen(tuner))
    {
        openTool(tuner);
    }
    if (auto* entry = findLiveTool(tuner); entry != nullptr && entry->component != nullptr)
    {
        // The only place the shell still names a specific tool: presets are a
        // tuner concept, and there is no general "apply preset" in the
        // contract to route them through.
        if (auto* component = dynamic_cast<TunerComponent*>(entry->component.get()))
        {
            component->applyPreset(preset);
        }
    }
}

AppSettings::State MainComponent::captureSettingsState()
{
    // Refreshes every instance's saved bounds and settings from its live
    // component, so the legacy fields captured below are already current.
    captureActiveWorkspace();
    if (settingsWindow != nullptr)
    {
        savedSettingsBounds = settingsWindow->getBounds();
    }

    AppSettings::State state;
    state.theme = currentTheme;
    state.microphoneMuted =
        sessionMicrophoneMuteEnabled ? persistedMicrophoneMuted : audioInputService.isMuted();
    state.inputGain = audioInputService.inputGain();
    captureLegacyToolState(state);
    state.settingsBounds = savedSettingsBounds.toString();
    state.fullscreenMode = selectedFullscreenMode;
    WorkspaceSessionLifecycle::captureActive(workspaceCatalog, activeWorkspaceSnapshot);
    state.workspaceCatalog = workspaceCatalog;
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
    applyLegacyToolState(loaded.state);
    workspaceCatalog = loaded.state.workspaceCatalog;
    selectedFullscreenMode = loaded.state.fullscreenMode;
    feedbackInvitationsDisabledState =
        settingsFile->getBoolValue("feedback.invitationsDisabled", false);

    if (auto xml = juce::parseXML(loaded.state.audioDeviceState); xml != nullptr)
    {
        pendingAudioDeviceState = std::move(xml);
    }

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

void MainComponent::restoreLoadedWorkspace()
{
    std::vector<WorkspaceBounds> displayAreas;
    const auto displays = juce::Desktop::getInstance().getDisplays().getRectangleList(true);
    for (const auto& area : displays)
    {
        displayAreas.push_back({area.getX(), area.getY(), area.getWidth(), area.getHeight()});
    }

    auto restored = WorkspaceSessionLifecycle::restore(std::move(workspaceCatalog), displayAreas);
    workspaceCatalog = std::move(restored.catalog);
    if (!applyWorkspaceSnapshot(workspaceCatalog.active))
    {
        WorkspaceSessionLifecycle::resetActive(workspaceCatalog);
        static_cast<void>(applyWorkspaceSnapshot(workspaceCatalog.active));
        restored.recovered = true;
    }

    if (restored.recovered)
    {
        saveSettings();
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
