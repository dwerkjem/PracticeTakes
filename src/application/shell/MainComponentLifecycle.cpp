#include "MainComponent.h"

#include "ui/feedback/FeedbackWindow.h"
#include "ui/main_window/MainTitleBar.h"
#include "ui/main_window/MicrophoneWarning.h"
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
#include "ui/performance/PerformanceLabWindow.h"
#endif
#include "ui/settings/SettingsWindow.h"
#include "ui/workspace/DockedToolPanel.h"
#include "ui/workspace/ToolWindow.h"

namespace
{
constexpr int microphoneWarningWidth = 470;
constexpr int microphoneWarningHeight = 118;
} // namespace

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
