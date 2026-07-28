#include "../../../MainComponent.h"

#include "../../../../../features/analysis/tuner/TunerComponent.h"
#include "../components/ToolWindow.h"
#include "../model/WorkspaceSnapshotApply.h"
#include "../model/WorkspaceSnapshotCapture.h"

#include <cmath>

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

[[nodiscard]] std::optional<double>
boundedPayloadNumber(const juce::var& value, double minimum, double maximum)
{
    if (!value.isInt() && !value.isInt64() && !value.isDouble())
    {
        return std::nullopt;
    }

    const auto number = static_cast<double>(value);
    if (!std::isfinite(number) || number < minimum || number > maximum)
    {
        return std::nullopt;
    }
    return number;
}

[[nodiscard]] std::optional<int>
boundedPayloadInteger(const juce::var& value, int minimum, int maximum)
{
    if (!value.isInt() && !value.isInt64())
    {
        return std::nullopt;
    }

    const auto number = static_cast<int>(value);
    if (number < minimum || number > maximum)
    {
        return std::nullopt;
    }
    return number;
}

[[nodiscard]] std::optional<AppDefaults::TunerSettings>
decodeTunerPayload(const std::optional<ToolSettingsPayload>& payload)
{
    if (!payload.has_value() || payload->version != 1)
    {
        return AppDefaults::tunerDefaults();
    }

    const auto decoded = juce::JSON::parse(juce::String::fromUTF8(payload->data.c_str()));
    if (decoded.getDynamicObject() == nullptr)
    {
        return std::nullopt;
    }

    const auto displayMode = boundedPayloadInteger(decoded["displayMode"], 1, 3);
    const auto easing = boundedPayloadNumber(decoded["easing"], 0.02, 1.0);
    const auto averaging = boundedPayloadNumber(decoded["averaging"], 1.0, 15.0);
    const auto noteSwitch = boundedPayloadNumber(decoded["noteSwitchSemitones"], 0.1, 1.5);
    const auto dropout = boundedPayloadNumber(decoded["dropoutFrames"], 1.0, 20.0);
    const auto duration = boundedPayloadNumber(decoded["graphDurationSeconds"], 5.0, 60.0);
    if (!displayMode.has_value() || !easing.has_value() || !averaging.has_value() ||
        !noteSwitch.has_value() || !dropout.has_value() || !duration.has_value())
    {
        return std::nullopt;
    }

    return AppDefaults::TunerSettings{
        *displayMode, *easing, *averaging, *noteSwitch, *dropout, *duration};
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

bool MainComponent::applyWorkspaceSnapshot(const WorkspaceSnapshot& snapshot)
{
    const auto bindings = std::vector<WorkspaceSnapshotApply::ToolBinding>{
        {"tuner", static_cast<WorkspaceLayoutState::Tool>(ToolType::tuner)},
        {"spectrogram", static_cast<WorkspaceLayoutState::Tool>(ToolType::spectrogram)},
        {"harmonic-analyzer", static_cast<WorkspaceLayoutState::Tool>(ToolType::harmonics)},
    };
    auto plan = WorkspaceSnapshotApply::plan(snapshot, WorkspaceToolRegistry{}, bindings);
    if (!plan.has_value())
    {
        return false;
    }

    auto tunerSettings = std::optional<AppDefaults::TunerSettings>{};
    for (const auto& directive : plan->tools)
    {
        if (directive.id == "tuner")
        {
            tunerSettings = decodeTunerPayload(directive.settings);
            if (!tunerSettings.has_value())
            {
                return false;
            }
        }
    }

    return WorkspaceSnapshotApply::execute(
        std::move(*plan), automaticSettingsSaveEnabled,
        WorkspaceSnapshotApply::Callbacks{
            [this]
            {
                detachWorkspaceContainer();
                for (const auto tool : allToolTypes)
                {
                    detachToolPresentation(tool);
                }
            },
            [this](std::unique_ptr<WorkspaceLayoutState::Node> root)
            {
                const auto replaced = workspaceLayoutState.replaceRoot(std::move(root));
                jassert(replaced);
                juce::ignoreUnused(replaced);
            },
            [this, &tunerSettings](const WorkspaceSnapshotApply::ToolDirective& directive)
            {
                const auto tool = static_cast<ToolType>(directive.tool);
                if (tool == ToolType::tuner)
                {
                    jassert(tunerSettings.has_value());
                    savedTunerSettings = *tunerSettings;
                }
                if (directive.presentation == WorkspaceToolState::Presentation::closed)
                {
                    componentFor(tool).reset();
                    static_cast<void>(stateFor(tool).close());
                    return;
                }

                if (directive.presentation == WorkspaceToolState::Presentation::floating)
                {
                    const auto bounds = juce::Rectangle<int>{
                        directive.floatingBounds.x, directive.floatingBounds.y,
                        directive.floatingBounds.width, directive.floatingBounds.height};
                    if (tool == ToolType::tuner)
                    {
                        savedTunerBounds = bounds;
                    }
                    else if (tool == ToolType::spectrogram)
                    {
                        savedSpectrogramBounds = bounds;
                    }
                    else
                    {
                        savedHarmonicBounds = bounds;
                    }
                }

                auto& component = componentFor(tool);
                if (component == nullptr)
                {
                    component = createToolComponent(tool);
                }
                else if (auto* tuner = dynamic_cast<TunerComponent*>(component.get()))
                {
                    tuner->applySettings(*tunerSettings);
                }
                static_cast<void>(stateFor(tool).present(directive.presentation));
            },
            [this](const WorkspaceSnapshotApply::ToolDirective& directive)
            {
                if (directive.presentation == WorkspaceToolState::Presentation::closed)
                {
                    return;
                }
                const auto tool = static_cast<ToolType>(directive.tool);
                constructToolPresentation(tool, directive.presentation);
                applyAppearanceToTool(tool);
            },
            [this] { rebuildWorkspaceContainer(); },
            [this](const std::optional<std::string>& focusedTool)
            {
                if (focusedTool == "tuner")
                {
                    focusTool(ToolType::tuner);
                }
                else if (focusedTool == "spectrogram")
                {
                    focusTool(ToolType::spectrogram);
                }
                else if (focusedTool == "harmonic-analyzer")
                {
                    focusTool(ToolType::harmonics);
                }
                else
                {
                    captureActiveWorkspace();
                }
            },
        });
}
