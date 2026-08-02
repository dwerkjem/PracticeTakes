#include "MainComponent.h"

#include "ui/feedback/FeedbackWindow.h"
#include "ui/main_window/MainTitleBar.h"
#include "ui/main_window/MicrophoneWarning.h"
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
#include "ui/performance/PerformanceLabWindow.h"
#endif
#include "ui/settings/SettingsWindow.h"
#include "ui/workspace/components/DockedToolPanel.h"
#include "ui/workspace/components/ToolWindow.h"

namespace
{
constexpr int microphoneWarningWidth = 470;
constexpr int microphoneWarningHeight = 118;

juce::String settingsFolderName()
{
#if JUCE_LINUX || JUCE_BSD
    // PropertiesFile puts Linux settings straight under $HOME, which leaves a
    // visible folder among the user's own directories. An absolute folderName
    // overrides that, so resolve the XDG config location instead.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("PracticeTakes")
        .getFullPathName();
#else
    return "PracticeTakes";
#endif
}

void migrateLegacySettingsFile(const juce::File& destination)
{
#if JUCE_LINUX || JUCE_BSD
    if (destination.existsAsFile())
    {
        return;
    }

    const auto legacyFolder =
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("PracticeTakes");
    const auto legacyFile = legacyFolder.getChildFile("PracticeTakes.settings");

    if (!legacyFile.existsAsFile() || !destination.getParentDirectory().createDirectory().wasOk())
    {
        return;
    }

    if (legacyFile.moveFileTo(destination))
    {
        // Removes the directory only when it is empty, so anything else the
        // user kept there survives.
        legacyFolder.deleteFile();
    }
#else
    juce::ignoreUnused(destination);
#endif
}
} // namespace

MainComponent::MainComponent(bool muteMicrophoneForSession)
    : sessionMicrophoneMuteEnabled(muteMicrophoneForSession)
{
    setOpaque(true);
    audioInputService.addChangeListener(this);

    juce::PropertiesFile::Options storageOptions;
    storageOptions.applicationName = "PracticeTakes";
    storageOptions.filenameSuffix = ".settings";
    storageOptions.folderName = settingsFolderName();
    storageOptions.osxLibrarySubFolder = "Application Support";
    storageOptions.commonToAllUsers = false;
    storageOptions.millisecondsBeforeSaving = -1;
    storageOptions.storageFormat = juce::PropertiesFile::storeAsXML;
    migrateLegacySettingsFile(storageOptions.getDefaultFile());
    applicationProperties.setStorageParameters(storageOptions);
    loadSettings();
    persistedMicrophoneMuted = audioInputService.isMuted();
    if (sessionMicrophoneMuteEnabled)
    {
        audioInputService.setMuted(true);
    }

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
    settingsWindow.reset();
    feedbackWindow.reset();
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
    performanceLabWindow.reset();
#endif
    workspaceContainers.clear();
    tunerDock.reset();
    spectrogramDock.reset();
    harmonicDock.reset();
    harmonicWindow.reset();
    spectrogramWindow.reset();
    tunerWindow.reset();
    spectrogramComponent.reset();
    harmonicComponent.reset();
    tunerComponent.reset();
    microphoneWarning.reset();
    applicationProperties.closeFiles();
    setLookAndFeel(nullptr);
}

void MainComponent::configureTopButtons()
{
    fileButton.setTooltip("File actions will be added later.");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    toolsButton.onClick = [this] { showToolsMenu(); };
    helpButton.onClick = [this] { showHelpMenu(); };
    microphoneButton.setTitle("Global microphone mute control");
    microphoneButton.onClick = [this] { audioInputService.toggleMuted(); };

    // Component ids let the development-only test control channel address these
    // by name rather than by screen position, so the manual GUI harness never
    // synthesises a pointer or a key. The names must match the approved click
    // targets in src/application/testcontrol/ApprovedWindowStates.cpp -- a
    // rename that misses one shows up as a click failing, not as a click
    // silently landing on nothing.
    settingsButton.setComponentID("settings-button");
    toolsButton.setComponentID("tools-button");
    helpButton.setComponentID("help-button");
    microphoneButton.setComponentID("microphone-button");
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

AppSettings::FullscreenMode MainComponent::fullscreenMode() const noexcept
{
    return selectedFullscreenMode;
}

void MainComponent::initialiseAudioAfterLaunch()
{
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync(
        [safeThis]
        {
            if (safeThis == nullptr)
            {
                return;
            }

            if (safeThis->pendingAudioDeviceState != nullptr)
            {
                safeThis->audioInputService.applySavedDeviceState(
                    *safeThis->pendingAudioDeviceState);
            }
            else
            {
                safeThis->audioInputService.initialiseDefaultInput();
            }

            safeThis->pendingAudioDeviceState.reset();
            safeThis->updateMicrophoneStateControl();
            safeThis->updateMicrophoneWarning();
        });
}

void MainComponent::createMicrophoneWarning()
{
    microphoneWarning = std::make_unique<MicrophoneWarning>(
        [this] { showSettings(); }, [this] { dismissMicrophoneWarning(); });
    addChildComponent(*microphoneWarning);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(appPaletteFor(currentTheme).background);
}

void MainComponent::resized()
{
    const auto bounds = getLocalBounds();
    layoutWorkspace(bounds.reduced(18));

    if (microphoneWarning != nullptr)
    {
        auto warningArea = bounds.reduced(18);
        const auto availableWidth = juce::jmin(microphoneWarningWidth, warningArea.getWidth());
        microphoneWarning->setBounds(
            warningArea.removeFromTop(microphoneWarningHeight).removeFromRight(availableWidth));
        if (microphoneWarning->isVisible())
        {
            microphoneWarning->toFront(false);
        }
    }
}
