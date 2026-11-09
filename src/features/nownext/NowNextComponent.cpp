#include "NowNextComponent.h"
#include "../../application/localisation/Localisation.h"

#include "../../application/theme/MiniStaff.h"
#include "../../platform/pitch/TargetChoice.h"
#include "../../platform/score/PitchNames.h"
#include "../../platform/score/TargetNoteSelection.h"
#include "../../platform/transport/AudibleTick.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
// Yellow on black for high contrast (decision 3).
constexpr juce::uint32 contrastAccent = 0xffffd400;

// Below this the tool shows only the current name and the next (decision 3).
constexpr int compactWidth = 320;
constexpr int compactHeight = 300;

[[nodiscard]] juce::String utf8(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}
} // namespace

NowNextComponent::NowNextComponent(
    const score::CurrentScore& currentScore,
    transport::PracticeClock& practiceClock,
    std::function<int()> outputLatencyFrames)
    : scoreHolder(currentScore), clock(practiceClock), latency(std::move(outputLatencyFrames))
{
    setComponentID("now-next-tool");
    startTimerHz(refreshRateHz);
}

NowNextComponent::~NowNextComponent()
{
    stopTimer();
}

void NowNextComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    repaint();
}

void NowNextComponent::resetToDefaults()
{
    chosenPartId.reset();
    lookAhead = nownext::defaultLookAhead;
    highContrast = false;
    shownScore.reset();
    repaint();
}

std::optional<ToolSettingsPayload> NowNextComponent::captureSettings() const
{
    std::string data =
        "ahead=" + std::to_string(lookAhead) + ";contrast=" + (highContrast ? "1" : "0") +
        ";part=" + chosenPartId.value_or("");
    return ToolSettingsPayload{settingsVersion, data};
}

void NowNextComponent::applySettings(const ToolSettingsPayload& payload)
{
    resetToDefaults();
    if (payload.version != settingsVersion)
    {
        return;
    }
    juce::StringArray fields;
    fields.addTokens(juce::String(payload.data), ";", "");
    for (const juce::String& field : fields)
    {
        const juce::String key = field.upToFirstOccurrenceOf("=", false, false);
        const juce::String value = field.fromFirstOccurrenceOf("=", false, false);
        if (key == "ahead")
        {
            const auto ahead = static_cast<std::size_t>(std::max(0, value.getIntValue()));
            for (const std::size_t choice : nownext::lookAheadChoices)
            {
                if (choice == ahead)
                {
                    lookAhead = ahead;
                }
            }
        }
        else if (key == "contrast")
        {
            highContrast = value == "1";
        }
        else if (key == "part" && value.isNotEmpty())
        {
            chosenPartId = value.toStdString();
        }
    }
    repaint();
}

std::vector<ToolComponent::MenuEntry> NowNextComponent::optionsMenuEntries()
{
    MenuEntry ahead{l10n::text("nownext.menu.look-ahead"), {}};
    for (const std::size_t choice : nownext::lookAheadChoices)
    {
        ahead.submenu.push_back(MenuEntry{
            l10n::plural("nownext.menu.look-ahead.notes", static_cast<std::int64_t>(choice)),
            [this, choice]
            {
                lookAhead = choice;
                repaint();
            },
            lookAhead == choice});
    }

    MenuEntry follow{l10n::text("nownext.menu.follow"), {}};
    follow.submenu.push_back(MenuEntry{
        l10n::text("nownext.menu.follow.target"),
        [this]
        {
            chosenPartId.reset();
            repaint();
        },
        !chosenPartId.has_value()});
    if (const auto shown = scoreHolder.score())
    {
        for (const score::Part& candidate : shown->parts)
        {
            const std::string id = candidate.id;
            follow.submenu.push_back(MenuEntry{
                utf8(candidate.name.empty() ? id : candidate.name),
                [this, id]
                {
                    chosenPartId = id;
                    repaint();
                },
                chosenPartId == id});
        }
    }

    MenuEntry contrast{
        l10n::text("nownext.menu.high-contrast"),
        [this]
        {
            highContrast = !highContrast;
            repaint();
        },
        highContrast};
    return {ahead, follow, contrast};
}

bool NowNextComponent::showView(const juce::String& viewName)
{
    if (viewName == "high-contrast")
    {
        highContrast = true;
        repaint();
        return true;
    }
    if (viewName == "default")
    {
        highContrast = false;
        repaint();
        return true;
    }
    return false;
}

void NowNextComponent::refreshTimeline()
{
    const std::shared_ptr<const score::Score> current = scoreHolder.score();
    if (current == shownScore && chosenPartId == shownPartChoice)
    {
        return;
    }
    shownScore = current;
    shownPartChoice = chosenPartId;
    timeline.clear();
    part = 0;
    if (current == nullptr || current->parts.empty())
    {
        return;
    }

    // The chosen part by id, or else the target's part (decision 4).
    bool found = false;
    if (chosenPartId.has_value())
    {
        for (std::size_t index = 0; index < current->parts.size(); ++index)
        {
            if (current->parts[index].id == *chosenPartId)
            {
                part = index;
                found = true;
            }
        }
    }
    if (!found)
    {
        const TargetNoteSelection selection = selectTargetNote(current);
        part = selection.note.has_value() ? selection.note->partIndex : 0;
    }
    timeline = score::timelineOf(*current, part);
}

void NowNextComponent::timerCallback()
{
    refreshTimeline();
    const transport::ClockPosition position = clock.position();
    playing = position.state == transport::ClockState::playing;
    countingIn = position.countingIn;
    table = clock.currentTable();
    const double heard = transport::audibleTick(
        position.tick, playing, clock.timing(), table.get(),
        juce::Time::getMillisecondCounterHiRes(), latency ? latency() : 0);

    const nownext::View next = nownext::viewAt(timeline, heard, lookAhead);
    const bool moved =
        next.now != view.now || next.next != view.next ||
        std::abs(next.through - view.through) > 0.002 || std::abs(heard - tick) > 1.0;
    tick = heard;
    view = next;
    if (moved)
    {
        repaint();
    }
}

NowNextComponent::Colours NowNextComponent::colours() const
{
    if (highContrast)
    {
        return Colours{juce::Colours::black,         juce::Colour(0xff111111),
                       juce::Colours::white,         juce::Colour(0xffc8c8c8),
                       juce::Colour(contrastAccent), juce::Colour(0xff8a8a8a)};
    }
    const AppPalette palette = appPaletteFor(currentTheme);
    return Colours{palette.background, palette.panel,  palette.foreground,
                   palette.muted,      palette.accent, palette.outline};
}

juce::String NowNextComponent::nameOf(const score::TimelineItem& item) const
{
    if (!item.isNote())
    {
        return l10n::text("nownext.rest");
    }
    juce::StringArray names;
    for (const score::Pitch& pitch : item.pitches)
    {
        if (score::isConsistent(pitch))
        {
            names.add(utf8(score::spelledName(pitch)));
        }
        else if (const auto spelled = pitch::spellMidi(pitch.midiNoteNumber, true))
        {
            names.add(utf8(score::spelledName(*spelled)));
        }
    }
    return names.joinIntoString(" ");
}

juce::String NowNextComponent::beatsText(const score::TimelineItem& item) const
{
    if (table == nullptr || table->empty())
    {
        return {};
    }
    const double beats = transport::beatsBetween(
        *table, static_cast<double>(item.start), static_cast<double>(item.end));
    return l10n::plural(
        "nownext.beats", nownext::beatsPluralCount(beats),
        {{"beats", utf8(nownext::beatsFigure(
                       beats, l10n::numberStyleFor(l10n::current().languageCode())))}});
}

void NowNextComponent::paint(juce::Graphics& graphics)
{
    const Colours palette = colours();
    graphics.fillAll(palette.background);

    if (scoreHolder.score() == nullptr)
    {
        paintMessage(graphics, l10n::text("nownext.no-score"));
        return;
    }
    if (timeline.empty())
    {
        paintMessage(graphics, l10n::text("nownext.no-notes"));
        return;
    }

    auto bounds = getLocalBounds().reduced(12);
    if (getWidth() < scaledHeight(compactWidth) || getHeight() < scaledHeight(compactHeight))
    {
        paintCompact(graphics, bounds);
        return;
    }

    // The part, and where the clock is.
    auto header = bounds.removeFromTop(scaledHeight(22));
    graphics.setColour(palette.muted);
    graphics.setFont(textFont(appearance::TextRole::control));
    const score::Part& followed = shownScore->parts[part];
    graphics.drawText(
        utf8(followed.name.empty() ? followed.id : followed.name), header,
        juce::Justification::centredLeft);
    const juce::String state = l10n::text(
        countingIn ? l10n::key("nownext.state.counting-in")
        : playing  ? l10n::key("nownext.state.playing")
                   : l10n::key("nownext.state.ready"));
    graphics.drawText(state, header, juce::Justification::centredRight);
    bounds.removeFromTop(6);

    const int nextHeight = std::clamp(bounds.getHeight() * 2 / 5, 90, 180);
    paintNext(graphics, bounds.removeFromBottom(nextHeight));
    bounds.removeFromBottom(10);
    paintNow(graphics, bounds);
}

void NowNextComponent::paintMessage(juce::Graphics& graphics, const juce::String& text) const
{
    graphics.setColour(colours().muted);
    graphics.setFont(textFont(appearance::TextRole::subheading));
    graphics.drawFittedText(text, getLocalBounds().reduced(16), juce::Justification::centred, 3);
}

void NowNextComponent::paintNow(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const Colours palette = colours();
    const juce::Rectangle<float> card = area.toFloat();
    graphics.setColour(palette.card);
    graphics.fillRoundedRectangle(card, 10.0f);
    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(card.reduced(0.5f), 10.0f, highContrast ? 2.0f : 1.0f);

    auto inside = area.reduced(18, 14);

    // The time bar along the bottom, emptying as the note runs out.
    auto barArea = inside.removeFromBottom(scaledHeight(30));
    inside.removeFromBottom(6);

    // Nothing sounding: before the first note, in a count-in, or at the end.
    if (!view.now.has_value())
    {
        graphics.setColour(palette.muted);
        graphics.setFont(juce::FontOptions(
            std::clamp(static_cast<float>(inside.getHeight()) * 0.22f, 16.0f, 40.0f)));
        juce::String text = l10n::text("nownext.end");
        if (!view.next.empty())
        {
            const score::TimelineItem& first = timeline[view.next.front()];
            text = l10n::format("nownext.next", {{"note", nameOf(first)}});
            if (table != nullptr && !table->empty())
            {
                const double beats =
                    transport::beatsBetween(*table, tick, static_cast<double>(first.start));
                if (beats > 0.0)
                {
                    text = l10n::plural(
                        "nownext.next.in-beats",
                        beats <= 1.01 ? 1 : static_cast<std::int64_t>(std::ceil(beats - 0.01)),
                        {{"note", nameOf(first)}});
                }
            }
        }
        graphics.drawFittedText(text, inside, juce::Justification::centred, 2);
        return;
    }

    const score::TimelineItem& item = timeline[*view.now];
    const bool rest = !item.isNote();

    // The staff to the right of the name.
    const int staffWidth = rest ? 0 : std::min(260, inside.getWidth() / 3);
    auto staffArea = inside.removeFromRight(staffWidth);
    if (!rest)
    {
        paintStaff(graphics, staffArea.toFloat(), item);
    }

    // As large as the card allows, by its height and its width, so a
    // maximised window reads from across a room.
    const float nameSize = std::clamp(
        std::min(
            static_cast<float>(inside.getHeight()) * 0.42f,
            static_cast<float>(inside.getWidth()) * 0.16f) *
            (highContrast ? 1.1f : 1.0f),
        28.0f, 240.0f);
    auto nameArea = inside.removeFromTop(static_cast<int>(nameSize * 1.1f));
    graphics.setColour(rest ? palette.muted : palette.foreground);
    graphics.setFont(juce::FontOptions(nameSize, juce::Font::bold));
    graphics.drawFittedText(nameOf(item), nameArea, juce::Justification::centredLeft, 1, 0.6f);

    // The lyric, and what kind of note it is.
    graphics.setFont(juce::FontOptions(std::clamp(nameSize * 0.36f, 14.0f, 40.0f)));
    if (!item.lyric.empty())
    {
        graphics.setColour(palette.accent);
        graphics.drawFittedText(
            utf8(item.lyric), inside.removeFromTop(static_cast<int>(nameSize * 0.45f)),
            juce::Justification::centredLeft, 1);
    }
    juce::StringArray notes;
    notes.add(beatsText(item));
    if (item.tiedOver)
    {
        notes.add(l10n::text("nownext.tied"));
    }
    if (item.repeated)
    {
        notes.add(l10n::text("nownext.again"));
    }
    graphics.setColour(palette.muted);
    graphics.setFont(juce::FontOptions(std::clamp(nameSize * 0.24f, 13.0f, 26.0f)));
    notes.removeEmptyStrings();
    graphics.drawText(
        notes.joinIntoString(juce::String::fromUTF8(" \xC2\xB7 ")), inside.removeFromTop(28),
        juce::Justification::centredLeft);

    // The time bar.
    const auto bar = barArea.toFloat();
    graphics.setColour(palette.outline.withAlpha(0.5f));
    graphics.fillRoundedRectangle(bar.withHeight(10.0f), 5.0f);
    graphics.setColour(rest ? palette.muted : palette.accent);
    graphics.fillRoundedRectangle(
        bar.withHeight(10.0f).withWidth(bar.getWidth() * static_cast<float>(1.0 - view.through)),
        5.0f);
    if (table != nullptr && !table->empty())
    {
        const double left = transport::secondsBetween(*table, tick, static_cast<double>(item.end));
        graphics.setColour(palette.muted);
        graphics.setFont(textFont(appearance::TextRole::control));
        graphics.drawText(
            l10n::format("nownext.seconds-left", {{"seconds", l10n::number(left, 1)}}),
            bar.withTrimmedTop(12.0f), juce::Justification::centredRight);
    }
}

void NowNextComponent::paintNext(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const Colours palette = colours();
    graphics.setColour(palette.muted);
    graphics.setFont(textFont(appearance::TextRole::body));
    graphics.drawText(
        l10n::text("nownext.heading"), area.removeFromTop(scaledHeight(18)),
        juce::Justification::centredLeft);

    if (view.next.empty())
    {
        graphics.drawText(
            l10n::text("nownext.nothing-after"), area, juce::Justification::centredLeft);
        return;
    }

    // Cards of equal width; a phrase start takes a breath mark and a gap
    // before it. The strip changes only when "now" moves on (decision 3).
    constexpr int gap = 8;
    const int breath = scaledHeight(22);
    int breaths = 0;
    for (const std::size_t index : view.next)
    {
        breaths += timeline[index].phraseStart ? 1 : 0;
    }
    const auto count = static_cast<int>(lookAhead);
    const int width =
        std::max(40, (area.getWidth() - (gap * (count - 1)) - (breath * breaths)) / count);

    int x = area.getX();
    for (const std::size_t index : view.next)
    {
        const score::TimelineItem& item = timeline[index];
        if (item.phraseStart)
        {
            graphics.setColour(palette.accent);
            graphics.setFont(textFont(appearance::TextRole::readout, juce::Font::bold));
            graphics.drawText(
                juce::String::fromUTF8("\xE2\x80\x99"),
                juce::Rectangle<int>(x, area.getY() - 6, breath, scaledHeight(34)),
                juce::Justification::centred);
            x += breath;
        }
        const juce::Rectangle<float> card(
            static_cast<float>(x), static_cast<float>(area.getY()), static_cast<float>(width),
            static_cast<float>(area.getHeight()));
        const bool rest = !item.isNote();
        graphics.setColour(rest ? palette.card.withAlpha(0.5f) : palette.card);
        graphics.fillRoundedRectangle(card, 8.0f);
        graphics.setColour(palette.outline);
        graphics.drawRoundedRectangle(card.reduced(0.5f), 8.0f, highContrast ? 1.5f : 1.0f);

        auto inside = card.reduced(8.0f, 6.0f);
        const float nameSize =
            std::clamp(inside.getHeight() * 0.36f, 14.0f, highContrast ? 46.0f : 38.0f);
        graphics.setColour(rest ? palette.muted : palette.foreground);
        graphics.setFont(juce::FontOptions(nameSize, juce::Font::bold));
        graphics.drawFittedText(
            nameOf(item), inside.removeFromTop(nameSize * 1.15f).toNearestInt(),
            juce::Justification::centredLeft, 1, 0.5f);
        graphics.setFont(juce::FontOptions(std::clamp(nameSize * 0.45f, 12.0f, 20.0f)));
        if (!item.lyric.empty())
        {
            graphics.setColour(palette.accent);
            graphics.drawFittedText(
                utf8(item.lyric), inside.removeFromTop(nameSize * 0.6f).toNearestInt(),
                juce::Justification::centredLeft, 1);
        }
        graphics.setColour(palette.muted);
        juce::String detail = beatsText(item);
        if (item.tiedOver)
        {
            detail = l10n::format("nownext.detail.tied", {{"beats", detail}});
        }
        graphics.drawFittedText(
            detail, inside.removeFromTop(nameSize * 0.6f).toNearestInt(),
            juce::Justification::centredLeft, 1);

        // A note sung again: a badge in the corner.
        if (item.repeated)
        {
            const auto badgeScale = static_cast<float>(scaledHeight(100)) / 100.0f;
            const juce::Rectangle<float> badge(
                card.getRight() - (50.0f * badgeScale), card.getY() + 6.0f, 44.0f * badgeScale,
                18.0f * badgeScale);
            graphics.setColour(palette.accent);
            graphics.drawRoundedRectangle(badge, 9.0f, 1.2f);
            graphics.setFont(textFont(appearance::TextRole::axis, juce::Font::bold));
            graphics.drawText(l10n::text("nownext.again"), badge, juce::Justification::centred);
        }
        x += width + gap;
    }
}

void NowNextComponent::paintCompact(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const Colours palette = colours();
    auto bottom = area.removeFromBottom(std::min(scaledHeight(24), area.getHeight() / 4));
    const juce::String now =
        view.now.has_value() ? nameOf(timeline[*view.now]) : juce::String("--");
    graphics.setColour(palette.foreground);
    graphics.setFont(juce::FontOptions(
        std::clamp(static_cast<float>(area.getHeight()) * 0.6f, 14.0f, 80.0f), juce::Font::bold));
    graphics.drawFittedText(
        now, area.removeFromTop(area.getHeight() * 3 / 4), juce::Justification::centred, 1, 0.5f);

    // The time bar, then what is next.
    const auto bar =
        area.toFloat().withSizeKeepingCentre(static_cast<float>(area.getWidth()) * 0.8f, 6.0f);
    graphics.setColour(palette.outline.withAlpha(0.5f));
    graphics.fillRoundedRectangle(bar, 3.0f);
    graphics.setColour(palette.accent);
    graphics.fillRoundedRectangle(
        bar.withWidth(bar.getWidth() * static_cast<float>(1.0 - view.through)), 3.0f);

    graphics.setColour(palette.muted);
    graphics.setFont(textFont(appearance::TextRole::body));
    graphics.drawText(
        view.next.empty()
            ? juce::String()
            : l10n::format("nownext.next", {{"note", nameOf(timeline[view.next.front()])}}),
        bottom, juce::Justification::centred);
}

void NowNextComponent::paintStaff(
    juce::Graphics& graphics,
    juce::Rectangle<float> area,
    const score::TimelineItem& item) const
{
    const Colours palette = colours();
    const int reference = item.pitches.front().midiNoteNumber;
    const pitch::StaffClef clef =
        reference >= 57 ? pitch::StaffClef::treble : pitch::StaffClef::bass;

    // Every pitch of the chord at the staff's centre, filled in the accent.
    const float x = area.getCentreX();
    std::vector<MiniStaffNote> heads;
    heads.reserve(item.pitches.size());
    for (const score::Pitch& written : item.pitches)
    {
        score::Pitch spelled = written;
        if (!score::isConsistent(spelled))
        {
            spelled = pitch::spellMidi(written.midiNoteNumber, true).value_or(written);
        }
        heads.push_back({spelled, x, palette.accent, true});
    }

    // Larger than the Note View's, since the card can be read from across a
    // room, with ledger lines reaching a little wider.
    MiniStaffStyle style;
    style.lines = palette.foreground.withAlpha(0.75f);
    style.label = palette.muted;
    style.labelFontHeight = textSize(appearance::TextRole::axis);
    style.maxSpacing = 16.0f;
    style.ledgerReach = 1.2f;
    drawMiniStaff(graphics, area, clef, heads, style);
}
