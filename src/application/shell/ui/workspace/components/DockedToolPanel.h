#pragma once

#include "../../../MainComponent.h"
#include "ToolOptionsButton.h"

#include "application/tools/CompactPresentation.h"

#include <functional>
#include <utility>

class MainComponent::DockedToolPanel final : public juce::Component
{
  public:
    DockedToolPanel(
        const juce::String& title,
        juce::Component& toolContent,
        std::function<void(juce::Component&)> dragHandler,
        std::function<void()> floatHandler,
        std::function<void()> feedbackHandler,
        std::function<void()> closeHandler,
        std::function<void()> focusHandler,
        ToolComponent* tool = nullptr)
        : content(&toolContent), onDrag(std::move(dragHandler)), onFocus(std::move(focusHandler)),
          optionsButton(
              "Float in window",
              std::move(floatHandler),
              std::move(feedbackHandler),
              std::move(closeHandler),
              tool == nullptr ? std::function<std::vector<ToolComponent::MenuEntry>()>{} : [tool]
                  { return tool->optionsMenuEntries(); })
    {
        // Adopted, not owned. The tool keeps it and lays it out itself when
        // nothing takes it -- a floating tool has no header to put it in.
        if (tool != nullptr)
        {
            adoptedHeaderControl = tool->headerControl();

            if (adoptedHeaderControl != nullptr)
            {
                addAndMakeVisible(*adoptedHeaderControl);
            }
        }

        titleLabel.setText(title, juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(titleLabel);
        addAndMakeVisible(optionsButton);

        addAndMakeVisible(toolContent);
        toolContent.addMouseListener(this, true);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    ~DockedToolPanel() override
    {
        releaseContent();
    }

    void releaseContent()
    {
        // Handed back before the panel goes: the tool checks whether anyone
        // still has it, and a dangling parent would keep it from showing the
        // chooser itself.
        if (adoptedHeaderControl != nullptr)
        {
            removeChildComponent(adoptedHeaderControl);
            adoptedHeaderControl = nullptr;
        }

        if (content != nullptr)
        {
            content->removeMouseListener(this);
            removeChildComponent(content);
            content = nullptr;
        }
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto background = findColour(juce::ResizableWindow::backgroundColourId);
        graphics.fillAll(background);
        graphics.setColour(findColour(juce::ComboBox::outlineColourId));
        graphics.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);

        // Below the threshold the header goes entirely: a pane with no room for
        // its tool's display has none to spare for a title, a chooser, and a
        // button either. Right-click still reaches the options.
        const auto compactPane = compact::isCompact(getWidth(), getHeight());

        titleLabel.setVisible(!compactPane);
        optionsButton.setVisible(!compactPane);

        if (adoptedHeaderControl != nullptr)
        {
            adoptedHeaderControl->setVisible(!compactPane);
        }

        if (compactPane)
        {
            if (content != nullptr)
            {
                content->setBounds(bounds);
            }

            return;
        }

        auto header = bounds.removeFromTop(32);
        optionsButton.setBounds(header.removeFromRight(38));
        header.removeFromRight(4);

        // The tool's own control sits between the title and the options button,
        // taking a share of the header rather than a fixed width, so a narrow
        // pane gives it less instead of pushing the title out.
        if (adoptedHeaderControl != nullptr)
        {
            const auto width = juce::jlimit(120, 220, header.getWidth() / 2);
            adoptedHeaderControl->setBounds(header.removeFromRight(width));
            header.removeFromRight(8);
        }

        titleLabel.setBounds(header);
        bounds.removeFromTop(4);
        if (content != nullptr)
        {
            content->setBounds(bounds);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (onFocus)
        {
            onFocus();
        }

        // The options are hidden on a pane too small to spend a header row on
        // them, so this is how they are still reached. Available at every size,
        // not only the small ones -- a menu that appears only when the button
        // disappears is a menu nobody discovers.
        if (event.mods.isPopupMenu())
        {
            optionsButton.showOptions();

            return;
        }

        dragStarted = false;
        dragArmed = canStartDrag(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragArmed && !dragStarted && event.getDistanceFromDragStart() >= 5)
        {
            dragStarted = true;
            if (onDrag)
            {
                onDrag(*this);
            }
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragStarted = false;
        dragArmed = false;
    }

  private:
    [[nodiscard]] bool canStartDrag(const juce::MouseEvent& event) const
    {
        if (event.originalComponent == this)
        {
            return true;
        }
        if (content == nullptr || event.originalComponent != content)
        {
            return false;
        }

        constexpr int innerPadding = 14;
        const auto point = event.getEventRelativeTo(content).getPosition();
        return !content->getLocalBounds().reduced(innerPadding).contains(point);
    }

    juce::Component* content;
    juce::Label titleLabel;
    std::function<void(juce::Component&)> onDrag;
    std::function<void()> onFocus;
    ToolOptionsButton optionsButton;

    // Borrowed from the tool for as long as this panel exists.
    juce::Component* adoptedHeaderControl = nullptr;
    bool dragStarted = false;
    bool dragArmed = false;
};
