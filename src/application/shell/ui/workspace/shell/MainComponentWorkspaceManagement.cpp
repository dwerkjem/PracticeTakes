#include "../../../../configuration/SettingsImportTransaction.h"
#include "../../../../localisation/Localisation.h"
#include "../../../MainComponent.h"

#include <algorithm>
#include <limits>

namespace
{
// Why an action failed. Applied, cancelled and awaiting confirmation are not
// failures and never reach here; they fall to the general sentence.
[[nodiscard]] juce::String workspaceStatusMessage(
    NamedWorkspaceService::ResultStatus status,
    const WorkspaceActionText& action)
{
    using Status = NamedWorkspaceService::ResultStatus;
    switch (status)
    {
    case Status::invalidName:
        return l10n::format(
            "workspace.error.invalid-name",
            {{"most",
              l10n::number(static_cast<double>(NamedWorkspaceService::maximumNameCharacters), 0)}});
    case Status::reservedName:
        return l10n::text("workspace.error.reserved-name");
    case Status::notFound:
        return l10n::text("workspace.error.not-found");
    case Status::immutableBuiltIn:
        return l10n::text(action.builtIn);
    case Status::catalogFull:
        return l10n::text("workspace.error.catalogue-full");
    case Status::idGenerationFailed:
        return l10n::text("workspace.error.no-identifier");
    case Status::applied:
    case Status::cancelled:
    case Status::confirmationRequired:
        break;
    }
    return l10n::text("workspace.error.general");
}

void showWorkspaceAlert(
    juce::MessageBoxIconType icon,
    const juce::String& title,
    const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(icon, title, message, l10n::text("workspace.alert.ok"));
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

void MainComponent::runUiValidationScenario(const juce::String& scenario)
{
    juce::Timer::callAfterDelay(
        500,
        [safeThis = juce::Component::SafePointer<MainComponent>(this), scenario]
        {
            if (safeThis == nullptr)
            {
                return;
            }

            if (scenario == "prepare-restored")
            {
                safeThis->applyWorkspace(WorkspaceBuiltIns::performancePreparation().id);
                safeThis->saveNamedWorkspace(
                    "Representative restored workspace with a deliberately long visible name",
                    false);
                return;
            }
            if (scenario == "import-confirmation")
            {
                SettingsTransferModel model;
                model.state.workspaceCatalog.active =
                    WorkspaceBuiltIns::performancePreparation().snapshot;
                model.state.workspaceCatalog.activeSource =
                    WorkspaceBuiltIns::performancePreparation().id;
                model.state.workspaceCatalog.named.push_back(
                    {"ui-validation-workspace",
                     "Representative restored workspace with a deliberately long visible name",
                     WorkspaceBuiltIns::performancePreparation().snapshot});
                safeThis->confirmSettingsImport(std::move(model));
                return;
            }
            if (scenario == "import-validation-error")
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, l10n::text("settings.import.failed"),
                    l10n::text("settings.transfer.error.not-a-settings-file"));
                return;
            }
            if (scenario == "import-rollback-error")
            {
                safeThis->showSettingsImportResult(
                    {.status = SettingsImportStatus::rollbackFailed,
                     .error = std::string(l10n::key("settings.import.error.rollback-failed"))});
                return;
            }
            if (scenario == "long-workspace-name")
            {
                safeThis->showSaveWorkspaceDialog();
                if (auto* alert = dynamic_cast<juce::AlertWindow*>(
                        juce::AlertWindow::getCurrentlyModalComponent()))
                {
                    if (auto* editor = alert->getTextEditor("workspaceName"))
                    {
                        editor->setText(
                            "Representative restored workspace with a deliberately long visible "
                            "name");
                    }
                }
                return;
            }
            if (scenario == "disconnected-display")
            {
                auto snapshot = WorkspaceBuiltIns::vocalWarmUp().snapshot;
                snapshot.floating.push_back(
                    {"spectrogram", {std::numeric_limits<int>::max() - 2000, 250, 920, 700}});
                safeThis->workspaceCatalog.active = std::move(snapshot);
                safeThis->workspaceCatalog.activeSource.reset();
                safeThis->restoreLoadedWorkspace();
            }
        });
}

void MainComponent::applyWorkspace(const std::string& workspaceId)
{
    const auto previous = workspaceCatalog;
    NamedWorkspaceService service;
    const auto result = service.apply(workspaceCatalog, workspaceId);
    if (result.status != NamedWorkspaceService::ResultStatus::applied)
    {
        showWorkspaceResult(workspaceactions::apply, result);
        return;
    }

    if (!applyWorkspaceSnapshot(workspaceCatalog.active))
    {
        workspaceCatalog = previous;
        showWorkspaceAlert(
            juce::MessageBoxIconType::WarningIcon, l10n::text("workspace.apply.not-applied"),
            l10n::text("workspace.apply.not-applied.message"));
        return;
    }

    saveSettings();
}

void MainComponent::showSaveWorkspaceDialog()
{
    auto alert = std::make_unique<juce::AlertWindow>(
        l10n::text("workspace.save.title"), l10n::text("workspace.save.prompt"),
        juce::MessageBoxIconType::NoIcon, this);
    alert->setLookAndFeel(&appLookAndFeel);
    alert->addTextEditor("workspaceName", {}, l10n::text("workspace.name"));
    if (auto* editor = alert->getTextEditor("workspaceName"))
    {
        editor->setInputRestrictions(
            static_cast<int>(NamedWorkspaceService::maximumNameCharacters));
        editor->setTitle(l10n::text("workspace.name"));
        editor->setDescription(l10n::text("workspace.save.name.description"));
    }
    alert->addButton(
        l10n::text("workspace.dialog.save"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(
        l10n::text("workspace.dialog.cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

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
    auto* releasedAlert = alert.release();
    juce::ignoreUnused(releasedAlert);
}

void MainComponent::showRenameWorkspaceDialog()
{
    const auto* workspace = activeNamedWorkspace();
    if (workspace == nullptr)
    {
        showWorkspaceAlert(
            juce::MessageBoxIconType::InfoIcon, l10n::text("workspace.rename.unavailable"),
            l10n::text("workspace.rename.unavailable.message"));
        return;
    }

    auto alert = std::make_unique<juce::AlertWindow>(
        l10n::text("workspace.rename.title"), l10n::text("workspace.rename.prompt"),
        juce::MessageBoxIconType::NoIcon, this);
    alert->setLookAndFeel(&appLookAndFeel);
    alert->addTextEditor(
        "workspaceName", juce::String::fromUTF8(workspace->name.c_str()),
        l10n::text("workspace.name"));
    if (auto* editor = alert->getTextEditor("workspaceName"))
    {
        editor->setInputRestrictions(
            static_cast<int>(NamedWorkspaceService::maximumNameCharacters));
        editor->setTitle(l10n::text("workspace.name"));
        editor->setDescription(l10n::text("workspace.rename.name.description"));
        editor->selectAll();
    }
    alert->addButton(
        l10n::text("workspace.dialog.rename"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(
        l10n::text("workspace.dialog.cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

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
    auto* releasedAlert = alert.release();
    juce::ignoreUnused(releasedAlert);
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
            juce::MessageBoxIconType::WarningIcon, l10n::text("workspace.overwrite.question.title"),
            l10n::format("workspace.save.overwrite.question", {{"name", name.trim()}}),
            l10n::text("workspace.dialog.overwrite"), l10n::text("workspace.dialog.cancel"), this,
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
    showWorkspaceResult(workspaceactions::save, result, name);
}

void MainComponent::renameNamedWorkspace(const juce::String& name, bool confirmed)
{
    const auto* workspace = activeNamedWorkspace();
    if (workspace == nullptr)
    {
        showWorkspaceAlert(
            juce::MessageBoxIconType::InfoIcon, l10n::text("workspace.rename.unavailable"),
            l10n::text("workspace.rename.unavailable.message"));
        return;
    }

    const auto sourceId = workspace->id;
    NamedWorkspaceService service;
    const auto result = service.rename(workspaceCatalog, sourceId, name.toStdString(), confirmed);
    if (result.status == NamedWorkspaceService::ResultStatus::confirmationRequired)
    {
        const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, l10n::text("workspace.rename.replace.title"),
            l10n::format("workspace.rename.replace.question", {{"name", name.trim()}}),
            l10n::text("workspace.dialog.replace"), l10n::text("workspace.dialog.cancel"), this,
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
    showWorkspaceResult(workspaceactions::rename, result, name);
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
        juce::MessageBoxIconType::WarningIcon, l10n::text("workspace.overwrite.question.title"),
        l10n::format("workspace.overwrite.question", {{"name", name}}),
        l10n::text("workspace.dialog.overwrite"), l10n::text("workspace.dialog.cancel"), this,
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
                safeThis->showWorkspaceResult(workspaceactions::overwrite, result);
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
        juce::MessageBoxIconType::WarningIcon, l10n::text("workspace.delete.question.title"),
        l10n::format("workspace.delete.question", {{"name", name}}),
        l10n::text("workspace.dialog.delete"), l10n::text("workspace.dialog.cancel"), this,
        juce::ModalCallbackFunction::create(
            [safeThis, id](int response)
            {
                if (safeThis == nullptr || response == 0)
                {
                    return;
                }
                NamedWorkspaceService service;
                const auto result = service.remove(safeThis->workspaceCatalog, id, true);
                safeThis->showWorkspaceResult(workspaceactions::remove, result);
            }));
}

void MainComponent::confirmResetWorkspace()
{
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon, l10n::text("workspace.reset.question.title"),
        l10n::format(
            "workspace.reset.question",
            {{"workspace",
              l10n::text(WorkspaceBuiltIns::nameKey(WorkspaceBuiltIns::pitchPractice().id))}}),
        l10n::text("workspace.dialog.reset"), l10n::text("workspace.dialog.cancel"), this,
        juce::ModalCallbackFunction::create(
            [safeThis](int response)
            {
                if (safeThis != nullptr && response != 0)
                {
                    safeThis->resetLayout();
                    showWorkspaceAlert(
                        juce::MessageBoxIconType::InfoIcon, l10n::text("workspace.reset.done"),
                        l10n::format(
                            "workspace.reset.done.message",
                            {{"workspace", l10n::text(WorkspaceBuiltIns::nameKey(
                                               WorkspaceBuiltIns::pitchPractice().id))}}));
                }
            }));
}

void MainComponent::showWorkspaceResult(
    const WorkspaceActionText& action,
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
            juce::MessageBoxIconType::WarningIcon, l10n::text(action.failed),
            workspaceStatusMessage(result.status, action));
        return;
    }

    saveSettings();
    const auto detail = requestedName.isNotEmpty()
                            ? l10n::format(action.completedFor, {{"name", requestedName.trim()}})
                            : l10n::text(action.completed);
    showWorkspaceAlert(juce::MessageBoxIconType::InfoIcon, l10n::text(action.title), detail);
}
