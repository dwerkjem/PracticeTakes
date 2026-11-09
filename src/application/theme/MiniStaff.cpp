#include "MiniStaff.h"

#include "../localisation/Localisation.h"

#include <algorithm>
#include <cstdlib>

juce::String miniStaffClefName(pitch::StaffClef clef)
{
    switch (clef)
    {
    case pitch::StaffClef::treble:
        break;
    case pitch::StaffClef::bass:
        return l10n::text("staff.clef.bass");
    case pitch::StaffClef::trebleOctaveDown:
        return l10n::text("staff.clef.treble-octave-down");
    }
    return l10n::text("staff.clef.treble");
}

float miniStaffSpacing(juce::Rectangle<float> area, const MiniStaffStyle& style)
{
    return std::clamp(area.getHeight() / 14.0f, style.minSpacing, style.maxSpacing);
}

std::vector<int> miniStaffLedgerSteps(int steps)
{
    std::vector<int> ledgers;
    for (int ledger = 6; ledger <= std::abs(steps); ledger += 2)
    {
        ledgers.push_back(steps > 0 ? ledger : -ledger);
    }
    return ledgers;
}

void drawMiniStaff(
    juce::Graphics& graphics,
    juce::Rectangle<float> area,
    pitch::StaffClef clef,
    std::span<const MiniStaffNote> notes,
    const MiniStaffStyle& style)
{
    const float spacing = miniStaffSpacing(area, style);
    const float middle = area.getCentreY();
    const float left = area.getX() + 4.0f;
    const float right = area.getRight() - 4.0f;
    const auto yOf = [&](int steps)
    { return middle - (static_cast<float>(steps) * spacing * 0.5f); };

    graphics.setColour(style.lines);
    for (int line = -2; line <= 2; ++line)
    {
        graphics.drawHorizontalLine(static_cast<int>(yOf(line * 2)), left, right);
    }
    if (style.label.has_value())
    {
        graphics.setColour(*style.label);
        graphics.setFont(juce::FontOptions(style.labelFontHeight));
        graphics.drawText(
            miniStaffClefName(clef),
            juce::Rectangle<float>(left, middle - (spacing * 7.0f), right - left, 14.0f),
            juce::Justification::centredLeft);
    }

    const float reach = spacing * style.ledgerReach;
    const float headWidth = spacing * style.headWidth;
    const float headHeight = spacing * style.headHeight;
    for (const MiniStaffNote& note : notes)
    {
        const int steps = pitch::staffStepsOf(note.pitch, clef);
        const float x = note.x;
        const float y = yOf(steps);

        graphics.setColour(style.ledgersInNoteColour ? note.colour : style.lines);
        for (const int ledger : miniStaffLedgerSteps(steps))
        {
            graphics.drawHorizontalLine(static_cast<int>(yOf(ledger)), x - reach, x + reach);
        }

        const juce::Rectangle<float> head(
            x - (headWidth * 0.5f), y - (headHeight * 0.5f), headWidth, headHeight);
        graphics.setColour(note.colour);
        if (note.filled)
        {
            graphics.fillEllipse(head);
        }
        else
        {
            graphics.drawEllipse(head, 2.0f);
        }

        if (style.accidentals && note.pitch.alter != 0)
        {
            graphics.setFont(juce::FontOptions(spacing * 1.6f));
            graphics.drawText(
                juce::String::fromUTF8(note.pitch.alter > 0 ? "\xE2\x99\xAF" : "\xE2\x99\xAD"),
                juce::Rectangle<float>(
                    x - (spacing * 2.6f), y - spacing, spacing * 1.6f, spacing * 2.0f),
                juce::Justification::centred);
        }
    }
}
