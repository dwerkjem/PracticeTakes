#include "../../MainComponent.h"

#include "../../../../platform/pitch/TargetChoice.h"

#include "../../../../features/analysis/tuner/TunerComponent.h"
#include "../../../../features/analysis/tuner/TunerSettingsCodec.h"
#include "../../../configuration/AudioDeviceStateTransfer.h"
#include "../../../configuration/SettingsImportPersistence.h"
#include "../../../configuration/SettingsImportTransaction.h"
#include "../../../configuration/SettingsTransferCodec.h"
#include "../../../configuration/SettingsTransferFile.h"
#include "../../../localisation/Localisation.h"
#include "../../../localisation/NoteNames.h"
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
constexpr int demoModeMenuItemId = 7;
// Automatic, then each language in availableLanguages' order.
constexpr int firstLanguageMenuItemId = 100;

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
        lightSettingsMenuItemId, l10n::text("menu.settings.theme.light"), true,
        currentTheme == Theme::light);
    appearanceMenu.addItem(
        darkSettingsMenuItemId, l10n::text("menu.settings.theme.dark"), true,
        currentTheme == Theme::dark);

    juce::PopupMenu fullscreenMenu;
    fullscreenMenu.addItem(
        normalFullscreenMenuItemId, l10n::text("menu.settings.fullscreen.normal"), true,
        selectedFullscreenMode == AppSettings::FullscreenMode::normal);
    fullscreenMenu.addItem(
        kioskFullscreenMenuItemId, l10n::text("menu.settings.fullscreen.kiosk"), true,
        selectedFullscreenMode == AppSettings::FullscreenMode::kiosk);

    // The language, by its globe, each listed by its own name: the one menu a
    // reader may need without being able to read the rest of it.
    juce::PopupMenu languageMenu;
    std::vector<juce::String> languageCodes{juce::String()};
    languageMenu.addItem(
        firstLanguageMenuItemId, l10n::text("settings.appearance.language.automatic"), true,
        chosenLanguage.isEmpty());
    for (const auto& language : l10n::availableLanguages())
    {
        languageCodes.emplace_back(language.code);
        languageMenu.addItem(
            firstLanguageMenuItemId + static_cast<int>(languageCodes.size()) - 1, language.name,
            true, chosenLanguage == juce::String(language.code));
    }

    const auto ink = appLookAndFeel.findColour(juce::PopupMenu::textColourId);
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addSubMenu(
        l10n::text("settings.appearance.language"), languageMenu, true,
        appearance::iconForMenu(appearance::Icon::globe, ink));
    menu.addSubMenu(
        l10n::text("settings.appearance.title"), appearanceMenu, true,
        appearance::iconForMenu(appearance::Icon::palette, ink));
    menu.addSubMenu(
        l10n::text("menu.settings.fullscreen"), fullscreenMenu, true,
        appearance::iconForMenu(appearance::Icon::maximize, ink));
    {
        const bool muted = audioInputService.isMuted();
        juce::PopupMenu::Item mute(l10n::text(
            muted ? l10n::key("settings.audio.microphone.unmute")
                  : l10n::key("settings.audio.microphone.mute")));
        mute.itemID = muteSettingsMenuItemId;
        mute.image = appearance::iconForMenu(
            muted ? appearance::Icon::microphone : appearance::Icon::microphoneOff, ink);
        menu.addItem(std::move(mute));
    }
    {
        // Ticked rather than marked with its icon: it is a state to be in.
        juce::PopupMenu::Item demo(l10n::text("demo.menu"));
        demo.itemID = demoModeMenuItemId;
        demo.isTicked = demoMode;
        menu.addItem(std::move(demo));
    }
    menu.addSeparator();
    {
        juce::PopupMenu::Item open(l10n::text("menu.settings.open"));
        open.itemID = openSettingsMenuItemId;
        open.image = appearance::iconForMenu(appearance::Icon::settings, ink);
        menu.addItem(std::move(open));
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&settingsButton).withMinimumWidth(230),
        [safeThis, languageCodes](int selectedItemId)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            const int language = selectedItemId - firstLanguageMenuItemId;
            if (language >= 0 && language < static_cast<int>(languageCodes.size()))
            {
                const auto& code = languageCodes[static_cast<std::size_t>(language)];
                if (code != safeThis->chosenLanguage)
                {
                    safeThis->chooseLanguage(code);
                    safeThis->showLanguageRestartNote();
                }
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
            case demoModeMenuItemId:
                safeThis->setDemoMode(!safeThis->demoMode);
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
        audioInputService, midiInputService, practiceClock, referencePitch, currentTarget,
        takeRecorder,
        [safeThis]
        {
            // The metronome and the count-in are saved as they change, like
            // the MIDI keyboard.
            if (safeThis != nullptr)
            {
                safeThis->saveSettings(false);
            }
        },
        midiPedalDownThreshold, currentTheme,
        [safeThis](Theme theme)
        {
            if (safeThis != nullptr)
            {
                safeThis->setTheme(theme);
            }
        },
        appLookAndFeel.textScale(),
        [safeThis](int percent)
        {
            if (safeThis != nullptr)
            {
                safeThis->setTextScale(percent);
                safeThis->saveSettings(false);
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
            // Persist the keyboard choice as soon as it is made, rather than
            // waiting for an explicit save: a user who picks their piano and
            // closes the application should find it chosen next time.
            if (safeThis != nullptr)
            {
                safeThis->saveSettings(false);
            }
        },
        [safeThis](int threshold)
        {
            // Same reasoning as the keyboard choice above: a preference about a
            // particular pedal, kept as soon as it is changed.
            if (safeThis != nullptr)
            {
                safeThis->midiPedalDownThreshold = threshold;
                safeThis->saveSettings(false);
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

    settingsWindow->setLanguageHandler(
        chosenLanguage,
        [safeThis](const juce::String& code)
        {
            if (safeThis != nullptr)
            {
                safeThis->chooseLanguage(code);
            }
        });
    settingsWindow->setNamingHandler(
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->noteNamingChanged();
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
        l10n::text("settings.import"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.ptsettings", true);
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
                    juce::MessageBoxIconType::WarningIcon, l10n::text("settings.import.failed"),
                    l10n::text(staged.error));
                return;
            }
            safeThis->confirmSettingsImport(*staged.model);
        });
}

void MainComponent::confirmSettingsImport(SettingsTransferModel model)
{
    // What the bundle does not carry stays as it is here, so confirming an
    // older bundle resets nothing it never held.
    model = SettingsTransferCodec::withLocalPreferences(std::move(model), captureSettingsState());

    auto activeWorkspace = juce::String{l10n::text("settings.import.summary.customized-workspace")};
    if (const auto& activeSource = model.state.workspaceCatalog.activeSource;
        activeSource.has_value())
    {
        if (const auto* builtIn = WorkspaceBuiltIns::find(*activeSource))
        {
            activeWorkspace = l10n::text(WorkspaceBuiltIns::nameKey(builtIn->id));
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

    auto summary = l10n::format(
        "settings.import.summary",
        {{"theme",
          l10n::text(
              model.state.theme == Theme::dark ? l10n::key("settings.appearance.theme.dark")
                                               : l10n::key("settings.appearance.theme.light"))},
         {"microphone",
          l10n::text(
              model.state.microphoneMuted ? l10n::key("settings.import.summary.muted")
                                          : l10n::key("settings.import.summary.enabled"))},
         {"gain", l10n::number(model.state.inputGain, 2)},
         {"workspace", activeWorkspace},
         {"workspaces",
          l10n::number(static_cast<double>(model.state.workspaceCatalog.named.size()), 0)}});
    if (model.state.language != chosenLanguage)
    {
        // Said in the language on screen now, which is the one being left.
        summary << "\n\n" << l10n::text("settings.import.summary.language");
    }
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, l10n::text("settings.import.confirm.title"),
        summary, l10n::text("settings.import.confirm.import"),
        l10n::text("settings.import.confirm.cancel"), this,
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
            juce::MessageBoxIconType::WarningIcon, l10n::text("settings.import.failed"),
            l10n::text("settings.import.failed.no-settings-file"));
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
        result.status == SettingsImportStatus::imported ? l10n::text("settings.import.done")
                                                        : l10n::text("settings.import.failed"),
        result.status == SettingsImportStatus::imported ? l10n::text("settings.import.done.message")
                                                        : l10n::text(result.error));
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
            juce::MessageBoxIconType::WarningIcon, l10n::text("settings.export.failed"),
            l10n::text(encoded.error));
        return;
    }

    settingsTransferChooser = std::make_unique<juce::FileChooser>(
        l10n::text("settings.export"),
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
                    ? l10n::text("settings.export.done")
                    : l10n::text("settings.export.failed"),
                result.status == SettingsTransferExportStatus::exported
                    ? l10n::text("settings.export.done.message")
                    : l10n::text(result.error));
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
    setTextScale(state.textScalePercent);
    selectedFullscreenMode = state.fullscreenMode;
    feedbackInvitationsDisabledState = model.feedbackInvitationsDisabled;
    audioInputService.setMuted(state.microphoneMuted);
    audioInputService.setInputGain(static_cast<float>(state.inputGain));
    applyLegacyToolState(state);
    savedSettingsBounds = validWindowBounds(state.settingsBounds);
    workspaceCatalog = state.workspaceCatalog;

    // Every setting travels, the devices too. One that is not here falls back
    // as a missing device always does: the default input, and a MIDI keyboard
    // held as a selection that opens when it is plugged in.
    if (auto audioState = juce::parseXML(state.audioDeviceState); audioState != nullptr)
    {
        audioInputService.applySavedDeviceState(*audioState);
    }
    else
    {
        audioInputService.resetToDefaultInput();
    }
    applyPracticePreferences(state);

    // An open Settings window -- the import is chosen from it -- shows the
    // values it was built with, so it is rebuilt where it was.
    if (settingsWindow != nullptr)
    {
        closeSettings();
        showSettings();
    }
    updateMicrophoneStateControl();
    updateMicrophoneWarning();

    return applyWorkspaceSnapshot(workspaceCatalog.active);
}

void MainComponent::applyPracticePreferences(const AppSettings::State& state)
{
    // The language is read at startup, before any window is built; a changed
    // one shows from the next launch (MainComponentAppearance.cpp).
    chosenLanguage = state.language;
    l10n::setNoteNaming(
        {static_cast<score::NoteNaming>(state.noteNaming),
         static_cast<score::OctaveNumbering>(state.octaveNumbering)});
    l10n::setSpelling({state.keySignatureFifths, state.flatsWithoutKey});
    applySpellingKey();

    // The keyboard may not be plugged in yet, and that is not a failure: the
    // service holds the selection and opens it when it appears.
    midiInputService.selectDevice(state.midiInputDeviceIdentifier, state.midiInputDeviceName);
    midiPedalDownThreshold = state.midiPedalDownThreshold;
    practiceClock.setMetronomeEnabled(state.metronomeEnabled);
    practiceClock.setMetronomeVolume(static_cast<float>(state.metronomeVolume));
    practiceClock.setAccentEnabled(state.metronomeAccent);
    practiceClock.setCountInBars(state.countInBars);
    practiceClock.setPlaybackEnabled(state.playbackEnabled);
    practiceClock.setPlaybackVolume(static_cast<float>(state.playbackVolume));
    referencePitch.set(state.referencePitchHz);
    {
        evaluation::IntonationSettings intonation = takeRecorder.intonationSettings();
        intonation.toleranceCents = state.inTuneCents;
        intonation.nearPerfectCents = state.nearPerfectCents;
        intonation.outOfTuneCents = state.outOfTuneCents;
        takeRecorder.setIntonation(intonation);
    }
    takeRecorder.setMinimumLevelDb(state.minimumLevelDb);
    takeRecorder.setTimingLimits(
        {state.timingRightOnMs, state.timingOnTimeMs, state.timingOffBeatMs});
    takeRecorder.setCountInWhenPlaying(state.countInWhenRecordingDuringPlayback);
    currentTarget.keepChosenWhenScoreOpens = state.keepTargetWhenScoreOpens;
    currentTarget.followScoreWhilePlaying = state.targetFollowsScore;
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
    setTextScale(appearance::TextScale::defaultPercent);
    l10n::setNoteNaming({});
    l10n::setSpelling({});
    applySpellingKey();
    chosenLanguage = {};
    if (settingsWindow != nullptr)
    {
        settingsWindow->showNaming();
    }
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
    state.textScalePercent = appLookAndFeel.textScale();
    state.noteNaming = static_cast<int>(l10n::noteNaming().naming);
    state.language = chosenLanguage;
    state.octaveNumbering = static_cast<int>(l10n::noteNaming().octaves);
    state.keySignatureFifths = l10n::spelling().keyFifths;
    state.flatsWithoutKey = l10n::spelling().flats;
    state.microphoneMuted =
        sessionMicrophoneMuteEnabled ? persistedMicrophoneMuted : audioInputService.isMuted();
    state.inputGain = audioInputService.inputGain();
    captureLegacyToolState(state);
    state.settingsBounds = savedSettingsBounds.toString();
    state.fullscreenMode = selectedFullscreenMode;
    WorkspaceSessionLifecycle::captureActive(workspaceCatalog, activeWorkspaceSnapshot);
    state.workspaceCatalog = workspaceCatalog;
    state.midiInputDeviceIdentifier = midiInputService.selectedDeviceIdentifier();
    state.midiInputDeviceName = midiInputService.selectedDeviceName();
    state.midiPedalDownThreshold = midiPedalDownThreshold;
    state.metronomeEnabled = practiceClock.metronomeEnabled();
    state.metronomeVolume = practiceClock.metronomeVolume();
    state.metronomeAccent = practiceClock.accentEnabled();
    state.countInBars = practiceClock.countInBars();
    state.playbackEnabled = practiceClock.playbackMix().enabled;
    state.playbackVolume = practiceClock.playbackMix().volume;
    state.referencePitchHz = referencePitch.hertz();
    state.inTuneCents = takeRecorder.intonationSettings().toleranceCents;
    state.nearPerfectCents = takeRecorder.intonationSettings().nearPerfectCents;
    state.outOfTuneCents = takeRecorder.intonationSettings().outOfTuneCents;
    state.minimumLevelDb = takeRecorder.minimumLevelDb();
    {
        const evaluation::TimingLimits limits = takeRecorder.timingLimits();
        state.timingRightOnMs = limits.rightOnMs;
        state.timingOnTimeMs = limits.onTimeMs;
        state.timingOffBeatMs = limits.offBeatMs;
    }
    state.countInWhenRecordingDuringPlayback = takeRecorder.countInWhenPlaying();
    state.keepTargetWhenScoreOpens = currentTarget.keepChosenWhenScoreOpens;
    state.targetFollowsScore = currentTarget.followScoreWhilePlaying;
    if (const auto target = currentTarget.target();
        target != nullptr && target->origin == pitch::TargetOrigin::chosen)
    {
        state.chosenTargetSounding = juce::String(target->name);
        state.chosenTargetWritten = juce::String(target->writtenName);
    }

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
    // Set before anything is laid out, so the first layout is at this size.
    appLookAndFeel.setTextScale(loaded.state.textScalePercent);
    titleStripLookAndFeel.setTextScale(loaded.state.textScalePercent);
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

    applyPracticePreferences(loaded.state);
    if (const auto sounding = pitch::parseNoteName(loaded.state.chosenTargetSounding.toStdString()))
    {
        const auto written = pitch::parseNoteName(loaded.state.chosenTargetWritten.toStdString());
        currentTarget.publish(
            pitch::chosenTarget(*sounding, written.value_or(*sounding), "Chosen"));
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
