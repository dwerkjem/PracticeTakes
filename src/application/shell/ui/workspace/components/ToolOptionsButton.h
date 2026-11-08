#pragma once

#include <JuceHeader.h>

#include "application/tools/ToolComponent.h"
#include "application/tools/ToolMenuEntries.h"

#include <functional>
#include <utility>
#include <vector>

class ToolOptionsButton final : public juce::TextButton
{
  public:
    ToolOptionsButton(
        juce::String presentationActionText,
        std::function<void()> presentationHandler,
        std::function<void()> feedbackHandler,
        std::function<void()> closeHandler,
        std::function<std::vector<ToolComponent::MenuEntry>()> toolEntryProvider = {})
        : presentationAction(std::move(presentationActionText)),
          onPresentation(std::move(presentationHandler)), onFeedback(std::move(feedbackHandler)),
          onClose(std::move(closeHandler)), toolEntries(std::move(toolEntryProvider))
    {
        setButtonText("...");
        setTitle("Tool options");
        setTooltip("Show tool options");
        onClick = [this] { showOptions(); };
    }

  private:
    enum MenuItem
    {
        changePresentation = 1,
        giveFeedback,
        close,
        rename,

        // The tool's own entries are numbered from here, so adding a shell item
        // above can never collide with them.
        firstToolEntry = 100
    };

  public:
    // Adds "Rename..." to the menu. Set by the shell after construction rather
    // than through the constructor, so the existing presentations need no new
    // parameter to carry it (multi-instance-tools).
    void setRenameHandler(std::function<void()> handler)
    {
        onRename = std::move(handler);
    }

    // Shown by the button, and by a right-click on the tool -- which is the only
    // way to reach it once a pane is too small to show the button at all.
    void showOptions()
    {
        juce::PopupMenu menu;

        // The tool's entries first, and separated: they are what the menu is
        // usually opened for, while the shell's three are the same everywhere.
        auto entries = toolEntries ? toolEntries() : std::vector<ToolComponent::MenuEntry>{};

        // Depth-first, numbering only what can be chosen -- the same order
        // `tool_menu::entryNumbered` finds them in.
        int nextNumber = firstToolEntry;

        const auto addLevel =
            [&nextNumber](
                juce::PopupMenu& into, const std::vector<ToolComponent::MenuEntry>& level,
                const auto& self) -> void
        {
            for (const ToolComponent::MenuEntry& entry : level)
            {
                if (entry.submenu.empty())
                {
                    into.addItem(nextNumber++, entry.label, entry.enabled, entry.ticked);

                    continue;
                }

                juce::PopupMenu sub;
                self(sub, entry.submenu, self);
                into.addSubMenu(entry.label, sub, entry.enabled);
            }
        };

        addLevel(menu, entries, addLevel);

        if (!entries.empty())
        {
            menu.addSeparator();
        }

        menu.addItem(changePresentation, presentationAction);
        if (onRename)
        {
            menu.addItem(rename, "Rename...");
        }
        menu.addItem(giveFeedback, "Give feedback");
        menu.addSeparator();
        menu.addItem(close, "Close tool");

        const auto safeThis = juce::Component::SafePointer<ToolOptionsButton>(this);
        menu.showMenuAsync(
            // At the button when it is on screen; at the pointer when it is not,
            // which is the right-click case on a pane too small for a header.
            (isVisible() ? juce::PopupMenu::Options().withTargetComponent(this)
                         : juce::PopupMenu::Options().withMousePosition())
                .withMinimumWidth(180),
            [safeThis](int selectedItemId)
            {
                if (safeThis == nullptr)
                {
                    return;
                }

                std::function<void()> action;
                if (selectedItemId == changePresentation)
                {
                    action = safeThis->onPresentation;
                }
                else if (selectedItemId == giveFeedback)
                {
                    action = safeThis->onFeedback;
                }
                else if (selectedItemId == close)
                {
                    action = safeThis->onClose;
                }
                else if (selectedItemId == rename)
                {
                    action = safeThis->onRename;
                }
                else if (selectedItemId >= firstToolEntry)
                {
                    // Re-asked rather than captured: the menu was built when it
                    // opened, and a tool whose entries depend on its own state
                    // must not have a stale action run against it.
                    auto current = safeThis->toolEntries ? safeThis->toolEntries()
                                                         : std::vector<ToolComponent::MenuEntry>{};
                    const auto number = static_cast<std::size_t>(selectedItemId - firstToolEntry);

                    // A disabled entry cannot be chosen from the menu; checked
                    // again here because the entries were re-asked, and the
                    // state that disabled it may have changed since.
                    if (const ToolComponent::MenuEntry* entry =
                            tool_menu::entryNumbered(current, number);
                        entry != nullptr && entry->enabled)
                    {
                        action = entry->action;
                    }
                }

                if (action)
                {
                    action();
                }
            });
    }

  private:
    juce::String presentationAction;
    std::function<void()> onPresentation;
    std::function<void()> onFeedback;
    std::function<void()> onClose;
    std::function<void()> onRename;
    std::function<std::vector<ToolComponent::MenuEntry>()> toolEntries;
};
