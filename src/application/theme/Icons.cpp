#include "Icons.h"

#include <PracticeTakesUiIcons.h>

#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace appearance
{
namespace
{
constexpr const char* iconProperty = "practiceTakesIcon";

struct Entry
{
    Icon icon;
    std::string_view resource;
};

constexpr std::array<Entry, 48> resources{{
    {Icon::globe, "globe_svg"},
    {Icon::palette, "palette_svg"},
    {Icon::audioLines, "audiolines_svg"},
    {Icon::piano, "piano_svg"},
    {Icon::target, "target_svg"},
    {Icon::lifeBuoy, "lifebuoy_svg"},
    {Icon::sunMoon, "sunmoon_svg"},
    {Icon::textSize, "alargesmall_svg"},
    {Icon::music, "music_svg"},
    {Icon::listOrdered, "listordered_svg"},
    {Icon::microphone, "mic_svg"},
    {Icon::microphoneOff, "micoff_svg"},
    {Icon::volume, "volume2_svg"},
    {Icon::activity, "activity_svg"},
    {Icon::drum, "drum_svg"},
    {Icon::tally, "tally5_svg"},
    {Icon::keyboardMusic, "keyboardmusic_svg"},
    {Icon::refresh, "refreshcw_svg"},
    {Icon::footprints, "footprints_svg"},
    {Icon::sliders, "slidershorizontal_svg"},
    {Icon::timer, "timer_svg"},
    {Icon::waveform, "audiowaveform_svg"},
    {Icon::crosshair, "crosshair_svg"},
    {Icon::circleCheck, "circlecheck_svg"},
    {Icon::circleAlert, "circlealert_svg"},
    {Icon::volumeOff, "volumex_svg"},
    {Icon::pin, "pin_svg"},
    {Icon::locate, "locatefixed_svg"},
    {Icon::circleDot, "circledot_svg"},
    {Icon::message, "messagesquare_svg"},
    {Icon::wrench, "wrench_svg"},
    {Icon::layout, "layoutdashboard_svg"},
    {Icon::rotate, "rotateccw_svg"},
    {Icon::save, "save_svg"},
    {Icon::download, "download_svg"},
    {Icon::upload, "upload_svg"},
    {Icon::folderOpen, "folderopen_svg"},
    {Icon::settings, "settings_svg"},
    {Icon::help, "circlehelp_svg"},
    {Icon::menu, "menu_svg"},
    {Icon::maximize, "maximize_svg"},
    {Icon::minimize, "minimize_svg"},
    {Icon::play, "play_svg"},
    {Icon::pause, "pause_svg"},
    {Icon::stop, "square_svg"},
    {Icon::check, "check_svg"},
    {Icon::repeat, "repeat_svg"},
    {Icon::slidersVertical, "slidersvertical_svg"},
}};

[[nodiscard]] std::unique_ptr<juce::Drawable> parse(Icon icon, juce::Colour colour)
{
    const std::string_view name = iconResourceName(icon);
    if (name.empty())
    {
        return nullptr;
    }
    int size = 0;
    const char* data = PracticeTakesUiIcons::getNamedResource(std::string(name).c_str(), size);
    if (data == nullptr || size <= 0)
    {
        return nullptr;
    }
    // Lucide draws in currentColor, which is the text's colour where an icon
    // stands beside text; JUCE's parser takes a colour, so it is written in.
    const auto svg =
        juce::String::fromUTF8(data, size)
            .replace("currentColor", "#" + colour.withAlpha(1.0f).toDisplayString(false));
    const std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(svg);
    if (xml == nullptr)
    {
        return nullptr;
    }
    auto drawable = juce::Drawable::createFromSVG(*xml);
    if (drawable != nullptr && colour.getAlpha() < 255)
    {
        drawable->setAlpha(colour.getFloatAlpha());
    }
    return drawable;
}
} // namespace

std::string_view iconResourceName(Icon icon) noexcept
{
    for (const Entry& entry : resources)
    {
        if (entry.icon == icon)
        {
            return entry.resource;
        }
    }
    return {};
}

const juce::Drawable* iconDrawable(Icon icon, juce::Colour colour)
{
    static std::map<std::pair<int, juce::uint32>, std::unique_ptr<juce::Drawable>> cache;
    const auto key = std::make_pair(static_cast<int>(icon), colour.getARGB());
    auto found = cache.find(key);
    if (found == cache.end())
    {
        found = cache.emplace(key, parse(icon, colour)).first;
    }
    return found->second.get();
}

std::unique_ptr<juce::Drawable> iconForMenu(Icon icon, juce::Colour colour)
{
    return parse(icon, colour);
}

juce::PopupMenu::Item menuItem(int itemId, const juce::String& text, Icon icon, juce::Colour ink)
{
    juce::PopupMenu::Item item(text);
    item.itemID = itemId;
    item.image = iconForMenu(icon, ink);
    return item;
}

void drawIcon(juce::Graphics& graphics, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    if (const juce::Drawable* drawable = iconDrawable(icon, colour))
    {
        drawable->drawWithin(graphics, area, juce::RectanglePlacement::centred, 1.0f);
    }
}

void TabIcon::paint(juce::Graphics& graphics)
{
    auto colour = juce::Colours::black;
    if (const auto* tab = dynamic_cast<const juce::TabBarButton*>(getParentComponent()))
    {
        // As LookAndFeel_V3 colours a tab's caption: the bar's text colour
        // when one is set, else the look-and-feel's, else the background's
        // contrast.
        colour = tab->getTabBackgroundColour().contrasting().withMultipliedAlpha(0.8f);
        const auto id = tab->isFrontTab() ? juce::TabbedButtonBar::frontTextColourId
                                          : juce::TabbedButtonBar::tabTextColourId;
        const auto& bar = tab->getTabbedButtonBar();
        if (bar.isColourSpecified(id))
        {
            colour = bar.findColour(id);
        }
        else if (getLookAndFeel().isColourSpecified(id))
        {
            colour = getLookAndFeel().findColour(id);
        }
    }
    const auto area = getLocalBounds().toFloat();
    const float size = std::min(area.getWidth(), area.getHeight()) * 0.62f;
    drawIcon(graphics, icon, area.withSizeKeepingCentre(size, size), colour);
}

void setIcon(juce::Component& component, Icon icon)
{
    if (iconOf(component) == icon)
    {
        return;
    }
    component.getProperties().set(iconProperty, static_cast<int>(icon));
    component.repaint();
}

Icon iconOf(const juce::Component& component)
{
    const juce::var& stored = component.getProperties()[iconProperty];
    return stored.isInt() ? static_cast<Icon>(static_cast<int>(stored)) : Icon::none;
}

void drawIconButtonText(
    juce::Graphics& graphics,
    juce::TextButton& button,
    const juce::Font& font,
    juce::Colour colour)
{
    const Icon icon = iconOf(button);
    const auto bounds = button.getLocalBounds().toFloat();
    const float iconSize = std::min(bounds.getHeight() * 0.55f, font.getHeight() * 1.15f);
    const float gap = iconSize * 0.4f;
    const float padding = std::min(8.0f, bounds.getHeight() * 0.3f);
    const juce::String& text = button.getButtonText();
    const float textWidth = juce::GlyphArrangement::getStringWidth(font, text);
    const float both = iconSize + (text.isEmpty() ? 0.0f : gap + textWidth);

    graphics.setFont(font);
    graphics.setColour(colour);
    if (both <= bounds.getWidth() - padding * 2.0f)
    {
        const float left = bounds.getCentreX() - both * 0.5f;
        drawIcon(
            graphics, icon,
            juce::Rectangle<float>(left, bounds.getCentreY() - iconSize * 0.5f, iconSize, iconSize),
            colour);
        graphics.drawText(
            text,
            juce::Rectangle<float>(
                left + iconSize + gap, bounds.getY(), textWidth + 2.0f, bounds.getHeight()),
            juce::Justification::centredLeft, false);
        return;
    }
    // No room for both: the icon alone, its words still in the tooltip.
    drawIcon(graphics, icon, bounds.withSizeKeepingCentre(iconSize, iconSize), colour);
}
} // namespace appearance
