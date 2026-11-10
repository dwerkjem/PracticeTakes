#include "MiniStaff.h"

#include "../localisation/Localisation.h"

#include <algorithm>
#include <cmath>
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

namespace
{
// The clef's glyph, and the line it sits on in steps from the middle line:
// the G clef curls round the second line up, the F clef's dots straddle the
// fourth.
struct ClefGlyph
{
    smufl::Glyph glyph;
    int steps;
};

[[nodiscard]] ClefGlyph clefGlyphOf(pitch::StaffClef clef)
{
    switch (clef)
    {
    case pitch::StaffClef::bass:
        return {smufl::Glyph::fClef, 2};
    case pitch::StaffClef::trebleOctaveDown:
        return {smufl::Glyph::gClef8vb, -2};
    case pitch::StaffClef::treble:
        break;
    }
    return {smufl::Glyph::gClef, -2};
}

// A key signature's accidentals, a space apart, starting this far after the
// clef.
constexpr float keyGapSpaces = 0.9f;
constexpr float keyStepSpaces = 1.0f;
} // namespace

std::vector<int> miniStaffKeySteps(int keyFifths, pitch::StaffClef clef)
{
    // Treble positions from the middle line; the bass clef writes each a third
    // (two steps) lower, and the tenor clef as the treble does.
    constexpr int sharpSteps[7] = {4, 1, 5, 2, -1, 3, 0};
    constexpr int flatSteps[7] = {0, 3, -1, 2, -2, 1, -3};
    const int shift = clef == pitch::StaffClef::bass ? -2 : 0;
    std::vector<int> steps;
    const int count = std::min(7, std::abs(keyFifths));
    for (int index = 0; index < count; ++index)
    {
        steps.push_back((keyFifths > 0 ? sharpSteps[index] : flatSteps[index]) + shift);
    }
    return steps;
}

int miniStaffKeyAlter(score::Step step, int keyFifths)
{
    using score::Step;
    constexpr Step sharpOrder[7] = {Step::f, Step::c, Step::g, Step::d, Step::a, Step::e, Step::b};
    constexpr Step flatOrder[7] = {Step::b, Step::e, Step::a, Step::d, Step::g, Step::c, Step::f};
    const int count = std::min(7, std::abs(keyFifths));
    for (int index = 0; index < count; ++index)
    {
        if ((keyFifths > 0 ? sharpOrder[index] : flatOrder[index]) == step)
        {
            return keyFifths > 0 ? 1 : -1;
        }
    }
    return 0;
}

namespace
{
// The clef starts this far into the staff, in spaces, as an engraved one does.
constexpr float clefIndentSpaces = 0.5f;

// And the first head this far after the widest clef ends, room for its sharp.
constexpr float clefClearanceSpaces = 2.8f;
} // namespace

float miniStaffClefWidth(juce::Rectangle<float> area, const MiniStaffStyle& style)
{
    if (style.font == nullptr || !style.font->isLoaded())
    {
        return 0.0f;
    }
    const float spacing = miniStaffSpacing(area, style);
    // The widest of the three, so the heads do not move when the clef changes.
    float widest = 0.0f;
    for (const auto clef :
         {pitch::StaffClef::treble, pitch::StaffClef::bass, pitch::StaffClef::trebleOctaveDown})
    {
        widest = std::max(widest, style.font->bounds(clefGlyphOf(clef).glyph).getRight());
    }
    const int signs = std::min(7, std::abs(style.keyFifths));
    const float key =
        signs == 0 ? 0.0f : keyGapSpaces + (static_cast<float>(signs) * keyStepSpaces);
    return 4.0f + (spacing * (clefIndentSpaces + widest + key + clefClearanceSpaces));
}

int miniStaffStepsAt(juce::Rectangle<float> area, const MiniStaffStyle& style, float y)
{
    const float spacing = miniStaffSpacing(area, style);
    return static_cast<int>(std::lround((area.getCentreY() - y) / (spacing * 0.5f)));
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
    if (style.font != nullptr && style.font->isLoaded())
    {
        // The outline is in staff spaces with its origin on the line the
        // clef names, so placing it is one scale and one move.
        const ClefGlyph glyph = clefGlyphOf(clef);
        juce::Path outline = style.font->outline(glyph.glyph);
        outline.applyTransform(juce::AffineTransform::scale(spacing).translated(
            left + (spacing * clefIndentSpaces), yOf(glyph.steps)));
        graphics.setColour(style.lines.withAlpha(1.0f));
        graphics.fillPath(outline);

        // The key signature, a space apart after the clef.
        const juce::Path& sign = style.font->outline(
            style.keyFifths > 0 ? smufl::Glyph::accidentalSharp : smufl::Glyph::accidentalFlat);
        float signX =
            left + (spacing *
                    (clefIndentSpaces + style.font->bounds(glyph.glyph).getRight() + keyGapSpaces));
        for (const int steps : miniStaffKeySteps(style.keyFifths, clef))
        {
            juce::Path placed = sign;
            placed.applyTransform(
                juce::AffineTransform::scale(spacing).translated(signX, yOf(steps)));
            graphics.fillPath(placed);
            signX += spacing * keyStepSpaces;
        }
    }
    else if (style.label.has_value())
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

        // Against the key: an accidental only where the note differs from it,
        // a natural where the key would sharpen or flatten its letter.
        const int keyAlter = miniStaffKeyAlter(note.pitch.step, style.keyFifths);
        if (style.accidentals && note.pitch.alter != keyAlter && style.font != nullptr &&
            style.font->isLoaded())
        {
            const smufl::Glyph glyph =
                note.pitch.alter > 0   ? smufl::Glyph::accidentalSharp
                : note.pitch.alter < 0 ? smufl::Glyph::accidentalFlat
                                       : smufl::Glyph::accidentalNatural;
            juce::Path placed = style.font->outline(glyph);
            const float glyphWidth = style.font->bounds(glyph).getRight() * spacing;
            placed.applyTransform(juce::AffineTransform::scale(spacing).translated(
                x - (headWidth * 0.5f) - (spacing * 0.3f) - glyphWidth, y));
            graphics.fillPath(placed);
        }
        else if (style.accidentals && note.pitch.alter != 0 && note.pitch.alter != keyAlter)
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
