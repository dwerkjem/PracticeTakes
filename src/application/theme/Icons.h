#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string_view>

// The interface's icons: Lucide outlines embedded from
// tools/packaging/icons/ui, drawn in the colour of the text beside them.
//
// An icon goes *with* words, not instead of them -- except where there is no
// room for the words, when a button keeps only its icon and its tooltip says
// the rest. Its reason is to be findable without reading: a setting is found by
// its picture, and the language setting by its globe, in a language the reader
// does not know.
namespace appearance
{
enum class Icon
{
    none,
    // Language, settings categories, and their rows.
    globe,
    palette,
    audioLines,
    piano,
    target,
    lifeBuoy,
    sunMoon,
    textSize,
    music,
    listOrdered,
    microphone,
    microphoneOff,
    volume,
    activity,
    drum,
    tally,
    keyboardMusic,
    refresh,
    footprints,
    sliders,
    timer,
    waveform,
    crosshair,
    circleCheck,
    circleAlert,
    volumeOff,
    pin,
    locate,
    circleDot,
    message,
    wrench,
    layout,
    rotate,
    save,
    download,
    upload,
    // The window's own controls.
    folderOpen,
    settings,
    help,
    menu,
    maximize,
    minimize,
    play,
    pause,
    stop,
    check,
    repeat,
    slidersVertical
};

// The embedded SVG's resource name ("globe_svg"), or empty for none.
[[nodiscard]] std::string_view iconResourceName(Icon icon) noexcept;

// The icon in `colour`, parsed once per colour and kept. Null for none, or if
// the data is missing -- a missing icon leaves its words, never a hole.
// Message thread.
[[nodiscard]] const juce::Drawable* iconDrawable(Icon icon, juce::Colour colour);

// A copy, for a popup menu item, which owns its icon.
[[nodiscard]] std::unique_ptr<juce::Drawable> iconForMenu(Icon icon, juce::Colour colour);

// A popup menu item with its icon. A ticked item shows its tick instead, so
// a choice among several (a theme, a language) is better left without one.
[[nodiscard]] juce::PopupMenu::Item
menuItem(int itemId, const juce::String& text, Icon icon, juce::Colour ink);

// Draws the icon centred in `area`.
void drawIcon(
    juce::Graphics& graphics,
    Icon icon,
    juce::Rectangle<float> area,
    juce::Colour colour);

// Gives a button, label, toggle or tab its icon; the look-and-feel draws it
// before the words.
void setIcon(juce::Component& component, Icon icon);

[[nodiscard]] Icon iconOf(const juce::Component& component);

// An icon on its own, as a component: a tab's, which JUCE places before the
// tab's caption and counts in the tab's width. Drawn in the tab's text colour.
class TabIcon final : public juce::Component
{
  public:
    explicit TabIcon(Icon iconToShow) : icon(iconToShow)
    {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& graphics) override;

  private:
    Icon icon;
};

// A text button's words with its icon before them, as AppLookAndFeel and the
// title strip's look-and-feel both draw it: icon and words centred together
// when both fit, the icon alone when they do not.
void drawIconButtonText(
    juce::Graphics& graphics,
    juce::TextButton& button,
    const juce::Font& font,
    juce::Colour colour);
} // namespace appearance
