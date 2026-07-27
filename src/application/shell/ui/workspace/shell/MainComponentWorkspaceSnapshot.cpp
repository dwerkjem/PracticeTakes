#include "../../../MainComponent.h"

#include "../../../../../features/analysis/tuner/TunerComponent.h"
#include "../components/ToolWindow.h"
#include "../model/WorkspaceSnapshotCapture.h"

namespace
{
[[nodiscard]] WorkspaceBounds workspaceBounds(const juce::Rectangle<int>& bounds)
{
    return {bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
}

[[nodiscard]] ToolSettingsPayload tunerPayload(const AppDefaults::TunerSettings& settings)
{
    auto* object = new juce::DynamicObject;
    object->setProperty("displayMode", settings.displayMode);
    object->setProperty("easing", settings.easing);
    object->setProperty("averaging", settings.averaging);
    object->setProperty("noteSwitchSemitones", settings.noteSwitchSemitones);
    object->setProperty("dropoutFrames", settings.dropoutFrames);
    object->setProperty("graphDurationSeconds", settings.graphDurationSeconds);
    return {1, juce::JSON::toString(juce::var(object)).toStdString()};
}
} // namespace

void MainComponent::captureActiveWorkspace()
{
    if (tunerWindow != nullptr)
    {
        savedTunerBounds = tunerWindow->getBounds();
    }
    if (spectrogramWindow != nullptr)
    {
        savedSpectrogramBounds = spectrogramWindow->getBounds();
    }
    if (harmonicWindow != nullptr)
    {
        savedHarmonicBounds = harmonicWindow->getBounds();
    }
    if (auto* tuner = dynamic_cast<TunerComponent*>(tunerComponent.get()))
    {
        savedTunerSettings = tuner->settings();
    }

    const auto observations = std::vector<WorkspaceSnapshotCapture::ToolObservation>{
        {"tuner", static_cast<WorkspaceLayoutState::Tool>(ToolType::tuner),
         tunerState.presentation(), workspaceBounds(savedTunerBounds),
         tunerPayload(savedTunerSettings)},
        {"spectrogram", static_cast<WorkspaceLayoutState::Tool>(ToolType::spectrogram),
         spectrogramState.presentation(), workspaceBounds(savedSpectrogramBounds), std::nullopt},
        {"harmonic-analyzer", static_cast<WorkspaceLayoutState::Tool>(ToolType::harmonics),
         harmonicState.presentation(), workspaceBounds(savedHarmonicBounds), std::nullopt},
    };

    auto focusedTool = std::string{"tuner"};
    if (currentTool == ToolType::spectrogram)
    {
        focusedTool = "spectrogram";
    }
    else if (currentTool == ToolType::harmonics)
    {
        focusedTool = "harmonic-analyzer";
    }

    activeWorkspaceSnapshot =
        WorkspaceSnapshotCapture::capture(workspaceLayoutState, observations, focusedTool);
}