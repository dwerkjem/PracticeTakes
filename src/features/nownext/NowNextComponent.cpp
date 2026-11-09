#include "NowNextComponent.h"

#include "../../platform/pitch/TargetChoice.h"
#include "../../platform/score/PitchNames.h"
#include "../../platform/score/TargetNoteSelection.h"
#include "../../platform/transport/AudibleTick.h"

#include <algorithm>
#include <cmath>

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
    MenuEntry ahead{"Look ahead", {}};
    for (const std::size_t choice : nownext::lookAheadChoices)
    {
        ahead.submenu.push_back(MenuEntry{
            juce::String(static_cast<int>(choice)) + " notes",
            [this, choice]
            {
                lookAhead = choice;
                repaint();
            },
            lookAhead == choice});
    }

    MenuEntry follow{"Follow part", {}};
    follow.submenu.push_back(MenuEntry{
        "The target's part",
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
        "High contrast",
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
        return "Rest";
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
    return utf8(nownext::beatsText(beats));
}

void NowNextComponent::paint(juce::Graphics& graphics)
{
    const Colours palette = colours();
    graphics.fillAll(palette.background);

    if (scoreHolder.score() == nullptr)
    {
        paintMessage(graphics, "Open a score to see the notes coming");
        return;
    }
    if (timeline.empty())
    {
        paintMessage(graphics, "This part has no notes to follow");
        return;
    }

    auto bounds = getLocalBounds().reduced(12);
    if (getWidth() < compactWidth || getHeight() < compactHeight)
    {
        paintCompact(graphics, bounds);
        return;
    }

    // The part, and where the clock is.
    auto header = bounds.removeFromTop(22);
    graphics.setColour(palette.muted);
    graphics.setFont(juce::FontOptions(14.0f));
    const score::Part& followed = shownScore->parts[part];
    graphics.drawText(
        utf8(followed.name.empty() ? followed.id : followed.name), header,
        juce::Justification::centredLeft);
    const juce::String state = countingIn ? "Counting in" : playing ? "Playing" : "Ready";
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
    graphics.setFont(juce::FontOptions(16.0f));
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
    auto barArea = inside.removeFromBottom(30);
    inside.removeFromBottom(6);

    // Nothing sounding: before the first note, in a count-in, or at the end.
    if (!view.now.has_value())
    {
        graphics.setColour(palette.muted);
        graphics.setFont(juce::FontOptions(
            std::clamp(static_cast<float>(inside.getHeight()) * 0.22f, 16.0f, 40.0f)));
        juce::String text = "End of the part";
        if (!view.next.empty())
        {
            const score::TimelineItem& first = timeline[view.next.front()];
            text = "Next: " + nameOf(first);
            if (table != nullptr && !table->empty())
            {
                const double beats =
                    transport::beatsBetween(*table, tick, static_cast<double>(first.start));
                if (beats > 0.0)
                {
                    text += "  in " + juce::String(static_cast<int>(std::ceil(beats - 0.01))) +
                            (beats <= 1.01 ? " beat" : " beats");
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
        notes.add("tied");
    }
    if (item.repeated)
    {
        notes.add("again");
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
        graphics.setFont(juce::FontOptions(14.0f));
        graphics.drawText(
            juce::String(left, 1) + " s left", bar.withTrimmedTop(12.0f),
            juce::Justification::centredRight);
    }
}

void NowNextComponent::paintNext(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const Colours palette = colours();
    graphics.setColour(palette.muted);
    graphics.setFont(juce::FontOptions(13.0f));
    graphics.drawText("Next", area.removeFromTop(18), juce::Justification::centredLeft);

    if (view.next.empty())
    {
        graphics.drawText("Nothing after this", area, juce::Justification::centredLeft);
        return;
    }

    // Cards of equal width; a phrase start takes a breath mark and a gap
    // before it. The strip changes only when "now" moves on (decision 3).
    constexpr int gap = 8;
    constexpr int breath = 22;
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
            graphics.setFont(juce::FontOptions(30.0f, juce::Font::bold));
            graphics.drawText(
                juce::String::fromUTF8("\xE2\x80\x99"),
                juce::Rectangle<int>(x, area.getY() - 6, breath, 34), juce::Justification::centred);
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
            detail += " tied";
        }
        graphics.drawFittedText(
            detail, inside.removeFromTop(nameSize * 0.6f).toNearestInt(),
            juce::Justification::centredLeft, 1);

        // A note sung again: a badge in the corner.
        if (item.repeated)
        {
            const juce::Rectangle<float> badge(
                card.getRight() - 50.0f, card.getY() + 6.0f, 44.0f, 18.0f);
            graphics.setColour(palette.accent);
            graphics.drawRoundedRectangle(badge, 9.0f, 1.2f);
            graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            graphics.drawText("again", badge, juce::Justification::centred);
        }
        x += width + gap;
    }
}

void NowNextComponent::paintCompact(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const Colours palette = colours();
    auto bottom = area.removeFromBottom(std::min(24, area.getHeight() / 4));
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
    graphics.setFont(juce::FontOptions(13.0f));
    graphics.drawText(
        view.next.empty() ? juce::String() : "Next: " + nameOf(timeline[view.next.front()]), bottom,
        juce::Justification::centred);
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

    const float spacing = std::clamp(area.getHeight() / 14.0f, 5.0f, 16.0f);
    const float middle = area.getCentreY();
    const float left = area.getX() + 4.0f;
    const float right = area.getRight() - 4.0f;
    graphics.setColour(palette.foreground.withAlpha(0.75f));
    for (int line = -2; line <= 2; ++line)
    {
        graphics.drawHorizontalLine(
            static_cast<int>(middle - (static_cast<float>(line) * spacing)), left, right);
    }
    graphics.setColour(palette.muted);
    graphics.setFont(juce::FontOptions(11.0f));
    graphics.drawText(
        clef == pitch::StaffClef::treble ? "Treble" : "Bass",
        juce::Rectangle<float>(left, middle - (spacing * 7.0f), right - left, 14.0f),
        juce::Justification::centredLeft);

    const float x = (left + right) * 0.5f;
    for (const score::Pitch& written : item.pitches)
    {
        score::Pitch spelled = written;
        if (!score::isConsistent(spelled))
        {
            spelled = pitch::spellMidi(written.midiNoteNumber, true).value_or(written);
        }
        const int steps = pitch::staffStepsOf(spelled, clef);
        const float y = middle - (static_cast<float>(steps) * spacing * 0.5f);
        graphics.setColour(palette.foreground.withAlpha(0.75f));
        for (int ledger = 6; ledger <= std::abs(steps); ledger += 2)
        {
            const float ledgerY =
                middle - (static_cast<float>(steps > 0 ? ledger : -ledger) * spacing * 0.5f);
            graphics.drawHorizontalLine(
                static_cast<int>(ledgerY), x - spacing * 1.2f, x + spacing * 1.2f);
        }
        const juce::Rectangle<float> head(
            x - spacing * 0.65f, y - spacing * 0.5f, spacing * 1.3f, spacing);
        graphics.setColour(palette.accent);
        graphics.fillEllipse(head);
        if (spelled.alter != 0)
        {
            graphics.setFont(juce::FontOptions(spacing * 1.6f));
            graphics.drawText(
                juce::String::fromUTF8(spelled.alter > 0 ? "\xE2\x99\xAF" : "\xE2\x99\xAD"),
                juce::Rectangle<float>(
                    x - (spacing * 2.6f), y - spacing, spacing * 1.6f, spacing * 2.0f),
                juce::Justification::centred);
        }
    }
}
