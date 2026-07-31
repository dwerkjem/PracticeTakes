#include "TestControlClicker.h"

namespace testcontrol
{
juce::Component* findComponentById(juce::Component& root, const juce::String& id)
{
    // An empty id would otherwise match the first component that has no id set
    // -- which is most of them, including the root. Searching for nothing must
    // find nothing.
    if (id.isEmpty())
    {
        return nullptr;
    }

    if (root.getComponentID() == id)
    {
        return &root;
    }

    for (int index = 0; index < root.getNumChildComponents(); ++index)
    {
        if (juce::Component* child = root.getChildComponent(index))
        {
            if (juce::Component* match = findComponentById(*child, id))
            {
                return match;
            }
        }
    }

    return nullptr;
}

bool isEffectivelyVisible(const juce::Component& component, const juce::Component& root)
{
    for (const juce::Component* current = &component; current != nullptr;
         current = current->getParentComponent())
    {
        if (!current->isVisible())
        {
            return false;
        }

        if (current == &root)
        {
            return true;
        }
    }

    // Ran out of ancestors without reaching `root`: the component is not in
    // this tree, so it is not ours to click.
    return false;
}

bool clickComponentById(juce::Component& root, const std::string& id)
{
    juce::Component* component = findComponentById(root, juce::String{id});

    if (component == nullptr)
    {
        return false;
    }

    if (!isEffectivelyVisible(*component, root) || !component->isEnabled())
    {
        return false;
    }

    auto* button = dynamic_cast<juce::Button*>(component);

    if (button == nullptr)
    {
        return false;
    }

    // Run the action directly rather than via Button::triggerClick.
    //
    // triggerClick posts the callback through the message queue, which makes a
    // click asynchronous and its effect unobservable to the caller -- the
    // channel could only ever answer "I asked", never "it happened". Invoking
    // the action synchronously is both what the harness needs and what makes
    // this testable without a message loop.
    if (button->onClick)
    {
        button->onClick();

        return true;
    }

    // A button with no onClick still has listeners worth reaching; fall back to
    // JUCE's own path rather than reporting a failure that is not one.
    button->triggerClick();

    return true;
}
} // namespace testcontrol
