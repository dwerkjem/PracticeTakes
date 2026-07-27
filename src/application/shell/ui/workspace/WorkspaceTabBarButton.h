#pragma once

#include <JuceHeader.h>

#include <functional>
#include <utility>

// A TabBarButton that acts as a fourth workspace drag source, alongside
// ToolDragHandle, DockedToolPanel, and ToolWindow: press-and-hold, then
// either move past the standard drag-start distance or hold Ctrl, picks the
// tab up as a workspace tool drag instead of switching tabs. A plain click
// (no qualifying movement) still switches tabs via the normal TabBarButton
// click path, since the base class handles that whenever a drag hasn't
// started.
class WorkspaceTabBarButton final : public juce::TabBarButton
{
  public:
    WorkspaceTabBarButton(
        const juce::String& name,
        juce::TabbedButtonBar& ownerBar,
        std::function<void(juce::Component&)> dragHandler)
        : juce::TabBarButton(name, ownerBar), onDrag(std::move(dragHandler))
    {
    }

    void paintButton(
        juce::Graphics& graphics,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override
    {
        // Render into a transparency layer while being dragged so the tab
        // looks "picked up" (lifted, semi-transparent) rather than fully
        // hidden -- gated purely on "is this tab's own drag active", not on
        // hover/highlight state.
        if (isBeingDragged)
        {
            graphics.beginTransparencyLayer(0.35f);
        }

        juce::TabBarButton::paintButton(
            graphics, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        if (isBeingDragged)
        {
            graphics.endTransparencyLayer();
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragArmed = true;
        dragStarted = false;
        juce::TabBarButton::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragArmed && !dragStarted &&
            (event.getDistanceFromDragStart() >= 5 || event.mods.isCtrlDown()))
        {
            dragStarted = true;
            isBeingDragged = true;
            setState(juce::Button::buttonNormal);
            repaint();
            if (onDrag)
            {
                onDrag(*this);
            }
            return;
        }

        if (!dragStarted)
        {
            juce::TabBarButton::mouseDrag(event);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        dragArmed = false;
        isBeingDragged = false;
        if (dragStarted)
        {
            // A drag happened -- suppress the tab-switch click a normal
            // Button::mouseUp would otherwise trigger on release.
            dragStarted = false;
            setState(juce::Button::buttonNormal);
            repaint();
            return;
        }
        juce::TabBarButton::mouseUp(event);
    }

  private:
    std::function<void(juce::Component&)> onDrag;
    bool dragArmed = false;
    bool dragStarted = false;
    bool isBeingDragged = false;
};
