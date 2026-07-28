#include "../../../MainComponent.h"

#include <algorithm>

namespace
{
[[nodiscard]] juce::String
workspaceStatusMessage(NamedWorkspaceService::ResultStatus status, const juce::String& action)
{
    using Status = NamedWorkspaceService::ResultStatus;
    switch (status)
    {
    case Status::applied:
        return action + " completed.";
    case Status::cancelled:
        return action + " was cancelled.";
    case Status::confirmationRequired:
        return action + " requires confirmation.";
    case Status::invalidName:
        return "Enter a workspace name between 1 and 80 characters.";
    case Status::reservedName:
        return "That name is reserved for a built-in workspace.";
    case Status::notFound:
        return "The selected workspace is no longer available.";
    case Status::immutableBuiltIn:
        return "Built-in workspaces cannot be " + action.toLowerCase() + ".";
    case Status::catalogFull:
        return "Delete a named workspace before saving another one.";
    case Status::idGenerationFailed:
        return "Practice Takes could not create a unique workspace identifier.";
    }
    jassertfalse;
    return "The workspace action could not be completed.";
}

void showWorkspaceAlert(
    juce::MessageBoxIconType icon,
    const juce::String& title,
    const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(icon, title, message, "OK");
}
} // namespace

const NamedWorkspace* MainComponent::activeNamedWorkspace() const
{
    if (!workspaceCatalog.activeSource.has_value())
    {
        return nullptr;
    }
    const auto& id = *workspaceCatalog.activeSource;
    const auto workspace = std::find_if(
        workspaceCatalog.named.begin(), workspaceCatalog.named.end(),
        [&id](const auto& candidate) { return candidate.id == id; });
    return workspace != workspaceCatalog.named.end() ? &*workspace : nullptr;
}

void MainComponent::applyWorkspace(const std::string& workspaceId)
{
    const auto previous = workspaceCatalog;
    NamedWorkspaceService service;
    const auto result = service.apply(workspaceCatalog, workspaceId);
    if (result.status != NamedWorkspaceService::ResultStatus::applied)
    {
        showWorkspaceResult("Apply workspace", result);
        return;
    }

    if (!applyWorkspaceSnapshot(workspaceCatalog.active))
    {
        workspaceCatalog = previous;
        showWorkspaceAlert(
            juce::MessageBoxIconType::WarningIcon, "Workspace not applied",
            "Practice Takes could not apply that workspace. Your current workspace is unchanged.");
        return;
    }

    saveSettings();
}

void MainComponent::showSaveWorkspaceDialog()
{
    auto alert = std::make_unique<juce::AlertWindow>(
        "Save workspace", "Enter a name for the current workspace.",
        juce::MessageBoxIconType::NoIcon, this);
    alert->setLookAndFeel(&appLookAndFeel);
    alert->addTextEditor("workspaceName", {}, "Workspace name");
    if (auto* editor = alert->getTextEditor("workspaceName"))
    {
        editor->setInputRestrictions(
            static_cast<int>(NamedWorkspaceService::maximumNameCharacters));
        editor->setTitle("Workspace name");
        editor->setDescription("Name for the workspace to save");
    }
    alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    auto* alertPointer = alert.get();
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, alertPointer](int result)
            {
                if (safeThis != nullptr && result == 1)
                {
                    safeThis->saveNamedWorkspace(
                        alertPointer->getTextEditorContents("workspaceName"), false);
                }
            }),
        true);
    static_cast<void>(alert.release());
}

void MainComponent::showRenameWorkspaceDialog()
{
    const auto* workspace = activeNamedWorkspace();
    if (workspace == nullptr)
    {
        showWorkspaceAlert(
            juce::MessageBoxIconType::InfoIcon, "Rename unavailable",
            "Select a named workspace before renaming it.");
        return;
    }

    auto alert = std::make_unique<juce::AlertWindow>(
        "Rename workspace", "Enter a new name for this workspace.",
        juce::MessageBoxIconType::NoIcon, this);
    alert->setLookAndFeel(&appLookAndFeel);
    alert->addTextEditor(
        "workspaceName", juce::String::fromUTF8(workspace->name.c_str()), "Workspace name");
    if (auto* editor = alert->getTextEditor("workspaceName"))
    {
        editor->setInputRestrictions(
            static_cast<int>(NamedWorkspaceService::maximumNameCharacters));
        editor->setTitle("Workspace name");
        editor->setDescription("New name for the selected workspace");
        editor->selectAll();
    }
    alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    auto* alertPointer = alert.get();
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, alertPointer](int result)
            {
                if (safeThis != nullptr && result == 1)
                {
                    safeThis->renameNamedWorkspace(
                        alertPointer->getTextEditorContents("workspaceName"), false);
                }
            }),
        true);
    static_cast<void>(alert.release());
}

void MainComponent::saveNamedWorkspace(const juce::String& name, bool confirmed)
{
    captureActiveWorkspace();
    WorkspaceSessionLifecycle::captureActive(workspaceCatalog, activeWorkspaceSnapshot);
    NamedWorkspaceService service;
    const auto result = service.save(workspaceCatalog, name.toStdString(), confirmed);
    if (result.status == NamedWorkspaceService::ResultStatus::confirmationRequired)
    {
        const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, "Overwrite workspace?",
            "A named workspace already uses ‘" + name.trim() +
                "’. Replace its saved layout with the current workspace?",
            "Overwrite", "Cancel", this,
            juce::ModalCallbackFunction::create(
                [safeThis, name](int response)
                {
                    if (safeThis != nullptr && response != 0)
                    {
                        safeThis->saveNamedWorkspace(name, true);
                    }
                }));
        return;
    }
    showWorkspaceResult("Save workspace", result, name);
}

void MainComponent::renameNamedWorkspace(const juce::String& name, bool confirmed)
{
    const auto* workspace = activeNamedWorkspace();
    if (workspace == nullptr)
    {
        showWorkspaceAlert(
            juce::MessageBoxIconType::InfoIcon, "Rename unavailable",
            "Select a named workspace before renaming it.");
        return;
    }

    const auto sourceId = workspace->id;
    NamedWorkspaceService service;
    const auto result = service.rename(workspaceCatalog, sourceId, name.toStdString(), confirmed);
    if (result.status == NamedWorkspaceService::ResultStatus::confirmationRequired)
    {
        const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, "Replace named workspace?",
            "Another named workspace already uses ‘" + name.trim() +
                "’. Delete that saved entry and use the name here?",
            "Replace", "Cancel", this,
            juce::ModalCallbackFunction::create(
                [safeThis, name](int response)
                {
                    if (safeThis != nullptr && response != 0)
                    {
                        safeThis->renameNamedWorkspace(name, true);
                    }
                }));
        return;
    }
    showWorkspaceResult("Rename workspace", result, name);
}

void MainComponent::confirmOverwriteWorkspace()
{
    const auto* workspace = activeNamedWorkspace();
    if (workspace == nullptr)
    {
        return;
    }

    const auto id = workspace->id;
    const auto name = juce::String::fromUTF8(workspace->name.c_str());
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon, "Overwrite workspace?",
        "Replace the saved ‘" + name + "’ layout with the current workspace?", "Overwrite",
        "Cancel", this,
        juce::ModalCallbackFunction::create(
            [safeThis, id](int response)
            {
                if (safeThis == nullptr || response == 0)
                {
                    return;
                }
                safeThis->captureActiveWorkspace();
                WorkspaceSessionLifecycle::captureActive(
                    safeThis->workspaceCatalog, safeThis->activeWorkspaceSnapshot);
                NamedWorkspaceService service;
                const auto result = service.overwrite(safeThis->workspaceCatalog, id, true);
                safeThis->showWorkspaceResult("Overwrite workspace", result);
            }));
}

void MainComponent::confirmDeleteWorkspace()
{
    const auto* workspace = activeNamedWorkspace();
    if (workspace == nullptr)
    {
        return;
    }

    const auto id = workspace->id;
    const auto name = juce::String::fromUTF8(workspace->name.c_str());
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon, "Delete workspace?",
        "Delete the saved ‘" + name + "’ workspace? The current live layout will remain open.",
        "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create(
            [safeThis, id](int response)
            {
                if (safeThis == nullptr || response == 0)
                {
                    return;
                }
                NamedWorkspaceService service;
                const auto result = service.remove(safeThis->workspaceCatalog, id, true);
                safeThis->showWorkspaceResult("Delete workspace", result);
            }));
}

void MainComponent::confirmResetWorkspace()
{
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon, "Reset workspace?",
        "Replace the current workspace with Pitch Practice? Named workspaces and global settings "
        "will not be changed.",
        "Reset", "Cancel", this,
        juce::ModalCallbackFunction::create(
            [safeThis](int response)
            {
                if (safeThis != nullptr && response != 0)
                {
                    safeThis->resetLayout();
                    showWorkspaceAlert(
                        juce::MessageBoxIconType::InfoIcon, "Workspace reset",
                        "Pitch Practice is now the active workspace.");
                }
            }));
}

void MainComponent::showWorkspaceResult(
    const juce::String& action,
    const NamedWorkspaceService::Result& result,
    const juce::String& requestedName)
{
    using Status = NamedWorkspaceService::ResultStatus;
    if (result.status == Status::cancelled || result.status == Status::confirmationRequired)
    {
        return;
    }
    if (result.status != Status::applied)
    {
        showWorkspaceAlert(
            juce::MessageBoxIconType::WarningIcon, action + " failed",
            workspaceStatusMessage(result.status, action));
        return;
    }

    saveSettings();
    auto detail = action + " completed.";
    if (requestedName.isNotEmpty())
    {
        detail = action + " completed for ‘" + requestedName.trim() + "’.";
    }
    showWorkspaceAlert(juce::MessageBoxIconType::InfoIcon, action, detail);
}
