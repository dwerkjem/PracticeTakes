#include "../../../MainComponent.h"

#include "../../../../tools/ToolInstanceAllocator.h"
#include "../../../../tools/ToolInstanceLabel.h"
#include "../../../../tools/ToolServices.h"
#include "../components/DockedToolPanel.h"
#include "../components/ToolWindow.h"

#include <algorithm>
#include <utility>

MainComponent::LiveTool* MainComponent::findLiveTool(const ToolInstanceId& instance)
{
    for (auto& entry : liveTools)
    {
        if (entry.id == instance)
        {
            return &entry;
        }
    }
    return nullptr;
}

const MainComponent::LiveTool* MainComponent::findLiveTool(const ToolInstanceId& instance) const
{
    for (const auto& entry : liveTools)
    {
        if (entry.id == instance)
        {
            return &entry;
        }
    }
    return nullptr;
}

MainComponent::LiveTool* MainComponent::findLiveTool(WorkspaceLayoutState::Tool handle)
{
    for (auto& entry : liveTools)
    {
        if (entry.handle == handle)
        {
            return &entry;
        }
    }
    return nullptr;
}

const ToolDefinition* MainComponent::definitionFor(const ToolInstanceId& instance) const
{
    return builtInToolRegistry().catalog().findForInstance(instance);
}

MainComponent::LiveTool* MainComponent::ensureLiveTool(const ToolInstanceId& instance)
{
    if (auto* existing = findLiveTool(instance))
    {
        return existing;
    }
    if (definitionFor(instance) == nullptr)
    {
        return nullptr;
    }

    LiveTool entry;
    entry.id = instance;
    entry.handle = static_cast<WorkspaceLayoutState::Tool>(nextToolHandle++);
    liveTools.push_back(std::move(entry));
    return &liveTools.back();
}

ToolInstanceId MainComponent::instanceIdToOpen(std::string_view toolId) const
{
    // The rules live in ToolInstanceAllocator so they can be tested without a
    // display; all the shell contributes is what is currently live.
    const auto isLive = [this](const ToolInstanceId& candidate)
    {
        const auto* existing = findLiveTool(candidate);
        return existing != nullptr && existing->state.isOpen();
    };
    std::size_t highestOrdinal = 1;
    for (const auto& entry : liveTools)
    {
        if (entry.id.toolId() == toolId)
        {
            highestOrdinal = std::max(highestOrdinal, entry.id.ordinal().value_or(1));
        }
    }
    return ToolInstanceAllocator::instanceToFocus(
               builtInToolRegistry().catalog(), toolId, isLive, highestOrdinal)
        .value_or(ToolInstanceId{});
}

std::vector<ToolInstanceId> MainComponent::openInstancesOf(std::string_view toolId) const
{
    std::vector<ToolInstanceId> open;
    for (const auto& entry : liveTools)
    {
        if (entry.id.toolId() == toolId && entry.state.isOpen())
        {
            open.push_back(entry.id);
        }
    }
    std::sort(
        open.begin(), open.end(), [](const ToolInstanceId& left, const ToolInstanceId& right)
        { return left.ordinal().value_or(1) < right.ordinal().value_or(1); });
    return open;
}

ToolInstanceAllocator::OpenAnother
MainComponent::canOpenAnotherInstance(std::string_view toolId) const
{
    const auto* definition = builtInToolRegistry().catalog().find(toolId);
    if (definition == nullptr)
    {
        return ToolInstanceAllocator::OpenAnother::singleInstance;
    }
    return ToolInstanceAllocator::canOpenAnother(
        *definition, openInstancesOf(toolId).size(), audioInputService.freeConsumerSlots());
}

AudioStreamUse::Use MainComponent::audioStreamUse() const
{
    // Counted from what is open rather than asked of the service, which holds
    // only pointers: whatever the open tools do not account for is the shared
    // pitch detector's slot (see AudioStreamUse).
    AudioStreamUse::Use use{
        AudioInputService::consumerCapacity(), audioInputService.freeConsumerSlots(), {}};
    for (const auto& definition : builtInToolRegistry().tools())
    {
        if (definition.readsAudioSamples)
        {
            use.tools.push_back({definition.displayName, openInstancesOf(definition.id).size()});
        }
    }
    return use;
}

void MainComponent::showNoAudioStreamAlert() const
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon, "No audio stream free",
        juce::String::fromUTF8(AudioStreamUse::explainFull(audioStreamUse()).c_str()), "OK");
}

void MainComponent::openAnotherInstance(std::string_view toolId)
{
    const auto verdict = canOpenAnotherInstance(toolId);
    if (verdict == ToolInstanceAllocator::OpenAnother::noAudioStreamFree)
    {
        showNoAudioStreamAlert();
        return;
    }
    if (verdict != ToolInstanceAllocator::OpenAnother::allowed)
    {
        return;
    }

    const auto isLive = [this](const ToolInstanceId& candidate)
    {
        const auto* existing = findLiveTool(candidate);
        return existing != nullptr && existing->state.isOpen();
    };
    const auto instance =
        ToolInstanceAllocator::nextInstanceId(builtInToolRegistry().catalog(), toolId, isLive);
    if (!instance.has_value())
    {
        return;
    }

    // A slot that a closed instance gave up is reused, but not what it held:
    // a new copy starts from the tool's defaults and default name, rather than
    // inheriting the settings and label of whichever instance last had the
    // ordinal.
    if (auto* entry = findLiveTool(*instance); entry != nullptr && !entry->state.isOpen())
    {
        entry->savedSettings.reset();
        entry->savedBounds = {};
        entry->label.clear();
    }
    openTool(*instance, WorkspaceToolState::Presentation::docked);
}

void MainComponent::showRenameToolDialog(const ToolInstanceId& instance)
{
    const auto* definition = definitionFor(instance);
    if (definition == nullptr || findLiveTool(instance) == nullptr)
    {
        return;
    }

    auto alert = std::make_unique<juce::AlertWindow>(
        "Rename " + toolName(instance),
        "A name for this " + juce::String::fromUTF8(definition->displayName.c_str()) +
            ". Leave it empty to use the default.",
        juce::MessageBoxIconType::NoIcon, this);
    alert->setLookAndFeel(&appLookAndFeel);
    alert->addTextEditor("toolName", toolName(instance), "Name");
    if (auto* editor = alert->getTextEditor("toolName"))
    {
        editor->setInputRestrictions(static_cast<int>(ToolInstanceLabel::maximumCharacters));
        editor->setTitle("Tool name");
        editor->setDescription("A name for this tool instance");
        editor->selectAll();
    }
    alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    auto* alertPointer = alert.get();
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, alertPointer, instance](int result)
            {
                if (safeThis != nullptr && result == 1)
                {
                    safeThis->renameTool(instance, alertPointer->getTextEditorContents("toolName"));
                }
            }),
        true);
    auto* releasedAlert = alert.release();
    juce::ignoreUnused(releasedAlert);
}

void MainComponent::renameTool(const ToolInstanceId& instance, const juce::String& requestedName)
{
    auto* entry = findLiveTool(instance);
    const auto* definition = definitionFor(instance);
    if (entry == nullptr || definition == nullptr)
    {
        return;
    }

    // Typing the default name back in is the same as clearing the label: the
    // instance keeps following the default if its ordinal's default ever
    // changes, rather than pinning today's text.
    auto label = ToolInstanceLabel::sanitise(requestedName.toStdString());
    if (label ==
        ToolInstanceLabel::defaultName(definition->displayName, instance.ordinal().value_or(1)))
    {
        label.clear();
    }
    if (label == entry->label)
    {
        return;
    }
    entry->label = std::move(label);

    // Renamed in place, never recreated: the tool keeps its analysis running.
    const auto name = toolName(instance);
    if (entry->dock != nullptr)
    {
        entry->dock->setDisplayTitle(name);
    }
    if (entry->window != nullptr)
    {
        entry->window->setDisplayTitle(name);
    }
    // Tab labels are made when the workspace container is built.
    rebuildWorkspaceContainer();
    captureActiveWorkspace();
}

void MainComponent::openToolById(
    std::string_view toolId,
    WorkspaceToolState::Presentation presentation)
{
    const auto instance = instanceIdToOpen(toolId);
    if (instance.empty())
    {
        return;
    }
    openTool(instance, presentation);
}

void MainComponent::openTool(
    const ToolInstanceId& instance,
    WorkspaceToolState::Presentation presentation)
{
    if (findLiveTool(instance) == nullptr && definitionFor(instance) == nullptr)
    {
        return;
    }

    currentTool = instance;
    if (toolIsOpen(instance))
    {
        focusTool(instance);
        return;
    }
    presentTool(instance, presentation);
}

void MainComponent::presentTool(
    const ToolInstanceId& instance,
    WorkspaceToolState::Presentation presentation)
{
    if (presentation == WorkspaceToolState::Presentation::closed)
    {
        closeTool(instance);
        return;
    }

    auto* entry = ensureLiveTool(instance);
    if (entry == nullptr)
    {
        return;
    }

    currentTool = instance;
    if (entry->component == nullptr)
    {
        // A tool that reads samples needs one of AudioInputService's fixed
        // consumer slots, and addListener has no way to say they are full. So
        // ask first, and say what is using them, rather than open a tool that
        // silently hears nothing.
        if (const auto* definition = definitionFor(instance);
            definition != nullptr && definition->readsAudioSamples &&
            audioInputService.freeConsumerSlots() == 0)
        {
            showNoAudioStreamAlert();
            return;
        }

        static_cast<void>(entry->state.present(presentation));

        const ToolServices services{
            audioInputService, pitchAnalysis, scoreImport.currentScoreHolder(), practiceClock,
            currentTheme};
        entry->component = builtInToolRegistry().create(instance.toolId(), services);
        if (entry->component == nullptr)
        {
            return;
        }
        if (entry->savedSettings.has_value())
        {
            entry->component->applySettings(*entry->savedSettings);
        }
    }
    else if (entry->state.presentation() == presentation)
    {
        focusTool(instance);
        return;
    }

    detachToolPresentation(instance);
    static_cast<void>(entry->state.present(presentation));

    // Keep the tiling tree in sync: docking places the tool somewhere in the
    // tree (auto-tabbing alongside whatever's already there, or becoming the
    // tree's sole pane if it's currently empty); floating removes it, since
    // only currently-docked tools are ever tracked by the tree. Drag-driven
    // docking calls insert() again right after this with a specific
    // pane/zone, which safely relocates the tool since insert() has move
    // (remove-then-place) semantics.
    if (presentation == WorkspaceToolState::Presentation::docked)
    {
        workspaceLayoutState.insert(
            entry->handle, std::nullopt, WorkspaceLayoutState::DropZone::centre);
    }
    else
    {
        workspaceLayoutState.remove(entry->handle);
    }

    constructToolPresentation(instance, presentation);
    rebuildWorkspaceContainer();
    applyAppearanceToTool(instance);
    focusTool(instance);
}

void MainComponent::constructToolPresentation(
    const ToolInstanceId& instance,
    WorkspaceToolState::Presentation presentation)
{
    jassert(presentation != WorkspaceToolState::Presentation::closed);
    auto* entry = findLiveTool(instance);
    if (entry == nullptr || entry->component == nullptr)
    {
        jassertfalse;
        return;
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    const auto closeHandler = [safeThis, instance]
    {
        if (safeThis != nullptr)
        {
            safeThis->closeTool(instance);
        }
    };
    const auto dragHandler = [safeThis, instance](juce::Component& source)
    {
        if (safeThis != nullptr)
        {
            safeThis->beginToolDrag(instance, source);
        }
    };
    const auto feedbackHandler = [safeThis, instance]
    {
        if (safeThis != nullptr)
        {
            safeThis->showFeedback(safeThis->toolName(instance));
        }
    };
    const auto focusHandler = [safeThis, instance]
    {
        if (safeThis != nullptr)
        {
            safeThis->recordToolFocus(instance);
        }
    };
    const auto renameHandler = [safeThis, instance]
    {
        if (safeThis != nullptr)
        {
            safeThis->showRenameToolDialog(instance);
        }
    };

    if (presentation == WorkspaceToolState::Presentation::docked)
    {
        const auto floatHandler = [safeThis, instance]
        {
            if (safeThis != nullptr)
            {
                safeThis->presentTool(instance, WorkspaceToolState::Presentation::floating);
            }
        };
        entry->dock = std::make_unique<DockedToolPanel>(
            toolName(instance), *entry->component, dragHandler, floatHandler, feedbackHandler,
            closeHandler, focusHandler, entry->component.get());
        entry->dock->setRenameHandler(renameHandler);
    }
    else
    {
        const auto dockHandler = [safeThis, instance]
        {
            if (safeThis != nullptr)
            {
                safeThis->presentTool(instance, WorkspaceToolState::Presentation::docked);
            }
        };
        const auto* definition = definitionFor(instance);
        const auto preferredSize =
            definition != nullptr
                ? juce::Point<
                      int>{definition->preferredSize.width, definition->preferredSize.height}
                : juce::Point<int>{900, 700};
        entry->window = std::make_unique<ToolWindow>(
            toolName(instance), *entry->component, preferredSize, dragHandler, dockHandler,
            feedbackHandler, closeHandler, focusHandler, entry->component.get());
        entry->window->setRenameHandler(renameHandler);
        if (!entry->savedBounds.isEmpty())
        {
            entry->window->setBounds(entry->savedBounds);
        }
    }
}

void MainComponent::focusTool(const ToolInstanceId& instance)
{
    if (auto* entry = findLiveTool(instance))
    {
        if (entry->window != nullptr)
        {
            entry->window->setVisible(true);
            entry->window->toFront(true);
        }
        else if (entry->dock != nullptr)
        {
            entry->dock->grabKeyboardFocus();
        }
    }
    recordToolFocus(instance);
}

void MainComponent::recordToolFocus(const ToolInstanceId& instance)
{
    currentTool = instance;
    captureActiveWorkspace();
}

void MainComponent::closeTool(const ToolInstanceId& instance)
{
    auto* entry = findLiveTool(instance);
    if (entry == nullptr || !entry->state.isOpen())
    {
        return;
    }

    // Capture before destruction: once the component is gone its settings are
    // unrecoverable, and reopening should bring the tool back as it was.
    if (entry->component != nullptr)
    {
        entry->savedSettings = entry->component->captureSettings();
    }

    detachToolPresentation(instance);
    workspaceLayoutState.remove(entry->handle);
    entry->component.reset();
    static_cast<void>(entry->state.close());
    rebuildWorkspaceContainer();
    captureActiveWorkspace();
    recordSuccessfulToolUse();
}

juce::String MainComponent::toolName(const ToolInstanceId& instance) const
{
    const auto* definition = definitionFor(instance);
    if (definition == nullptr)
    {
        return {};
    }

    // The user's label when there is one; otherwise the first instance keeps
    // the tool's plain name, which is what every existing workspace and
    // screenshot expects to see, and later ones carry their ordinal.
    const auto* entry = findLiveTool(instance);
    return juce::String::fromUTF8(ToolInstanceLabel::displayName(
                                      definition->displayName, instance.ordinal().value_or(1),
                                      entry != nullptr ? entry->label : std::string{})
                                      .c_str());
}

bool MainComponent::toolIsOpen(const ToolInstanceId& instance) const
{
    const auto* entry = findLiveTool(instance);
    return entry != nullptr && entry->state.isOpen();
}

void MainComponent::detachToolPresentation(const ToolInstanceId& instance)
{
    auto* entry = findLiveTool(instance);
    if (entry == nullptr)
    {
        return;
    }

    if (entry->window != nullptr)
    {
        entry->savedBounds = entry->window->getBounds();
        entry->window->releaseContent();
        entry->window.reset();
    }
    if (entry->dock != nullptr)
    {
        entry->dock->releaseContent();
        // Detach from whatever the dock's actual current parent is -- a
        // TabbedComponent needs its tab explicitly removed first (it keeps
        // its own bookkeeping of tab-index-to-content-component), while any
        // other parent (WorkspaceSplitPane, or this MainComponent directly
        // for a lone docked tool) can just have the child removed directly.
        if (auto* parent = entry->dock->getParentComponent())
        {
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(parent))
            {
                for (int index = tabs->getNumTabs(); --index >= 0;)
                {
                    if (tabs->getTabContentComponent(index) == entry->dock.get())
                    {
                        tabs->removeTab(index);
                    }
                }
            }
            else
            {
                parent->removeChildComponent(entry->dock.get());
            }
        }
        entry->dock.reset();
    }
}

void MainComponent::applyAppearanceToTool(const ToolInstanceId& instance)
{
    auto* entry = findLiveTool(instance);
    if (entry == nullptr)
    {
        return;
    }

    const auto palette = appPaletteFor(currentTheme);
    if (entry->window != nullptr)
    {
        entry->window->applyAppearance(&appLookAndFeel, palette.background);
    }
    if (entry->dock != nullptr)
    {
        entry->dock->setLookAndFeel(&appLookAndFeel);
        entry->dock->sendLookAndFeelChange();
        entry->dock->repaint();
    }

    // No dynamic_cast ladder: every tool answers setTheme through the contract,
    // so a tool the shell has never heard of is themed like any other.
    if (entry->component != nullptr)
    {
        entry->component->setTheme(currentTheme);
    }
}
