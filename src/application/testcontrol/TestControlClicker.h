#pragma once

#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

// Resolving an approved object to the live component that implements it, and
// running its action.
//
// Deliberately includes `juce_gui_basics` rather than `JuceHeader.h`, so this
// stays inside the module set `PracticeTakesTests` links and can be unit tested
// against real components with no display.
//
// This is the piece that makes "click" mean something without synthesising
// input. Nothing here moves a pointer, posts an X event, or knows a screen
// coordinate: an object is found by its component id and asked to act.
namespace testcontrol
{
// Depth-first search for a component whose id matches, starting at `root`
// itself. Null when nothing matches.
[[nodiscard]] juce::Component* findComponentById(juce::Component& root, const juce::String& id);

// Whether a component and every ancestor up to `root` is visible.
//
// `juce::Component::isVisible` reports only the component's own flag, so a
// visible child of a hidden parent would otherwise look clickable. The
// hamburger button is exactly this case -- it is added hidden and only shown
// below the collapsed-menu width.
[[nodiscard]] bool
isEffectivelyVisible(const juce::Component& component, const juce::Component& root);

// Ask the object named `id` to act, as though it had been clicked.
//
// Returns false, rather than doing nothing quietly, when the object is absent,
// hidden, disabled, or is not something that can be clicked. The channel turns
// that into an explicit failure, because a harness recording a check that never
// ran is worse than one that stops.
[[nodiscard]] bool clickComponentById(juce::Component& root, const std::string& id);
} // namespace testcontrol
