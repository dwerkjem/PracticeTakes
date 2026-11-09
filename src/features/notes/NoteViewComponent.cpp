#include "NoteViewComponent.h"

#include "../../application/localisation/Localisation.h"
#include "../../application/localisation/NoteNames.h"
#include "../../application/theme/MiniStaff.h"
#include "../../platform/score/PitchNames.h"

#include <cmath>
#include <vector>

namespace
{
// Purple, so a held key is never read as one of the four zone colours.
constexpr juce::uint32 heldColour = 0xff9b6bd6;

[[nodiscard]] juce::String nameOfMidi(int midi, int keyFifths)
{
    return l10n::heardNoteName(midi, keyFifths);
}
} // namespace

NoteViewComponent::NoteViewComponent(
    SharedPitchAnalysis& sharedPitchAnalysis,
    const pitch::ReferencePitch& applicationReferencePitch,
    pitch::CurrentTarget& applicationTarget,
    MidiInputService& midiInputService,
    std::function<pitch::ZoneLimits()> zoneLimitsSource)
    : pitchAnalysis(sharedPitchAnalysis), referencePitch(applicationReferencePitch),
      target(applicationTarget), midi(midiInputService), limits(std::move(zoneLimitsSource))
{
    setComponentID("note-view-tool");
    midi.addConsumer(this);
    startTimerHz(refreshRateHz);
}

NoteViewComponent::~NoteViewComponent()
{
    stopTimer();
    midi.removeConsumer(this);
}

AppPalette NoteViewComponent::palette() const
{
    return appPaletteFor(currentTheme);
}

pitch::ZoneLimits NoteViewComponent::zoneLimits() const
{
    return limits ? pitch::orderedLimits(limits()) : pitch::ZoneLimits{};
}

double NoteViewComponent::toleranceCents() const
{
    return zoneLimits().toleranceCents;
}

std::optional<pitch::IntonationZone> NoteViewComponent::zone() const
{
    const auto aimingAt = target.target();
    const auto limits = zoneLimits();
    return notes::currentZone(
        currentPitch, currentCents,
        aimingAt != nullptr ? std::optional<int>(aimingAt->midiNoteNumber) : std::nullopt,
        limits.toleranceCents, limits.nearPerfectCents, limits.outOfTuneCents);
}

void NoteViewComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    repaint();
}

void NoteViewComponent::resetToDefaults()
{
    clef = ClefChoice::automatic;
    trail.clear();
    repaint();
}

std::optional<ToolSettingsPayload> NoteViewComponent::captureSettings() const
{
    constexpr std::array<const char*, 4> names{"auto", "treble", "bass", "treble8vb"};
    return ToolSettingsPayload{
        settingsVersion, std::string("clef=") + names.at(static_cast<std::size_t>(clef))};
}

void NoteViewComponent::applySettings(const ToolSettingsPayload& payload)
{
    clef = ClefChoice::automatic;
    if (payload.version != settingsVersion)
    {
        return;
    }
    if (payload.data == "clef=treble")
    {
        clef = ClefChoice::treble;
    }
    else if (payload.data == "clef=bass")
    {
        clef = ClefChoice::bass;
    }
    else if (payload.data == "clef=treble8vb")
    {
        clef = ClefChoice::trebleOctaveDown;
    }
    repaint();
}

std::vector<ToolComponent::MenuEntry> NoteViewComponent::optionsMenuEntries()
{
    MenuEntry clefMenu{l10n::text("notes.menu.clef"), {}};
    const auto choice = [this](const juce::String& label, ClefChoice value)
    {
        return MenuEntry{
            label,
            [this, value]
            {
                clef = value;
                repaint();
            },
            clef == value};
    };
    clefMenu.submenu = {
        choice(l10n::text("notes.menu.clef.automatic"), ClefChoice::automatic),
        choice(l10n::text("notes.menu.clef.treble"), ClefChoice::treble),
        choice(l10n::text("notes.menu.clef.bass"), ClefChoice::bass),
        choice(l10n::text("notes.menu.clef.treble-octave-down"), ClefChoice::trebleOctaveDown)};
    return {clefMenu};
}

void NoteViewComponent::readKeyboard()
{
    for (;;)
    {
        const std::size_t count = midi.readEvents(this, keyBuffer.data(), keyBuffer.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            const midi::MidiEvent& event = keyBuffer.at(index);
            if (event.kind == midi::MidiEventKind::noteOn && event.velocity > 0)
            {
                heldKeys.insert(event.noteNumber);
            }
            else if (
                event.kind == midi::MidiEventKind::noteOff ||
                (event.kind == midi::MidiEventKind::noteOn && event.velocity == 0))
            {
                heldKeys.erase(event.noteNumber);
            }
        }
        if (count < keyBuffer.size())
        {
            break;
        }
    }
}

void NoteViewComponent::timerCallback()
{
    readKeyboard();

    // The same smoothing as the tuner, at the same reference, so the two agree.
    PitchTracker::Settings settings;
    settings.referenceHz = referencePitch.hertz();
    const auto analysis = pitchAnalysis.latestResult();
    const auto update = analysis.frequency > 0.0 ? tracker.detected(analysis.frequency, settings)
                                                 : tracker.missing(settings);
    if (update.hasSignal && update.hasDisplayedNote)
    {
        currentNote = update.displayedMidiNote;
        currentCents = update.displayedCents;
        currentPitch = static_cast<double>(update.displayedMidiNote) + (currentCents / 100.0);
    }
    else
    {
        currentNote.reset();
        currentPitch.reset();
    }
    trail.add(juce::Time::getMillisecondCounterHiRes() / 1000.0, currentPitch);

    std::vector<int> mustShow(heldKeys.begin(), heldKeys.end());
    if (currentNote.has_value())
    {
        mustShow.push_back(*currentNote);
    }
    if (const auto aimingAt = target.target())
    {
        mustShow.push_back(aimingAt->midiNoteNumber);
    }
    window = notes::windowFor(window, mustShow);
    repaint();
}

pitch::StaffClef NoteViewComponent::clefFor(int referenceMidi) const
{
    switch (clef)
    {
    case ClefChoice::treble:
        return pitch::StaffClef::treble;
    case ClefChoice::bass:
        return pitch::StaffClef::bass;
    case ClefChoice::trebleOctaveDown:
        return pitch::StaffClef::trebleOctaveDown;
    case ClefChoice::automatic:
        break;
    }
    return referenceMidi >= 60 ? pitch::StaffClef::treble : pitch::StaffClef::bass;
}

float NoteViewComponent::xOf(double midiPitch, juce::Rectangle<int> area) const
{
    const double first = notes::keyCentre(window.lowest) - 0.5;
    const double last = notes::keyCentre(window.highest) + 0.5;
    const double along = (notes::pitchX(midiPitch) - first) / (last - first);
    return static_cast<float>(area.getX()) +
           (static_cast<float>(along) * static_cast<float>(area.getWidth()));
}

NoteViewComponent::Areas NoteViewComponent::areas() const
{
    Areas result;
    auto bounds = getLocalBounds().reduced(12);
    result.reading = bounds.removeFromTop(scaledHeight(30));
    bounds.removeFromTop(6);

    // Small and docked: the keyboard alone (design decision 5).
    const bool compact = getWidth() < scaledHeight(240) || getHeight() < scaledHeight(160);
    if (compact)
    {
        result.keyboard = bounds;
        return result;
    }

    // The staff to the left; the trail above the keys to the right, sharing
    // their width so a line in the trail sits over its key.
    result.staff = bounds.removeFromLeft(std::min(160, bounds.getWidth() / 4));
    bounds.removeFromLeft(10);
    const int keyboardHeight = std::min(90, std::max(48, bounds.getHeight() / 3));
    result.keyboard = bounds.removeFromBottom(keyboardHeight);
    bounds.removeFromBottom(2);
    result.trail = bounds;
    return result;
}

void NoteViewComponent::paint(juce::Graphics& graphics)
{
    const AppPalette colours = palette();
    graphics.fillAll(colours.background);

    const Areas parts = areas();
    paintReading(graphics, parts.reading);
    if (!parts.staff.isEmpty())
    {
        paintStaff(graphics, parts.staff);
    }
    paintKeyboard(graphics, parts.keyboard);
    if (!parts.trail.isEmpty())
    {
        paintTrail(graphics, parts.trail);
    }
}

pitch::StaffClef NoteViewComponent::staffClef() const
{
    const auto aimingAt = target.target();
    return clefFor(currentNote.value_or(aimingAt != nullptr ? aimingAt->midiNoteNumber : 60));
}

MiniStaffStyle NoteViewComponent::staffStyle() const
{
    const AppPalette colours = palette();
    MiniStaffStyle style;
    style.lines = colours.foreground.withAlpha(0.75f);
    style.label = colours.muted;
    style.labelFontHeight = textSize(appearance::TextRole::axis);
    style.font = &musicFont.get();
    return style;
}

float NoteViewComponent::targetHeadX(juce::Rectangle<int> staff) const
{
    // The target hollow on the left, the note being sung filled on the right,
    // across the staff's lines after the clef (inset four pixels at the end).
    const float left =
        static_cast<float>(staff.getX()) + miniStaffClefWidth(staff.toFloat(), staffStyle());
    const float width = std::max(0.0f, static_cast<float>(staff.getRight()) - 4.0f - left);
    return left + (width * 0.35f);
}

float NoteViewComponent::sungHeadX(juce::Rectangle<int> staff) const
{
    const float left =
        static_cast<float>(staff.getX()) + miniStaffClefWidth(staff.toFloat(), staffStyle());
    const float width = std::max(0.0f, static_cast<float>(staff.getRight()) - 4.0f - left);
    return left + (width * 0.7f);
}

std::optional<int> NoteViewComponent::keyUnder(juce::Point<float> point) const
{
    const juce::Rectangle<float> keys = areas().keyboard.toFloat();
    if (!keys.contains(point) || keys.getWidth() <= 0.0f || keys.getHeight() <= 0.0f)
    {
        return std::nullopt;
    }
    return notes::keyAt(
        window, (point.x - keys.getX()) / keys.getWidth(),
        (point.y - keys.getY()) / keys.getHeight());
}

std::optional<score::Pitch> NoteViewComponent::staffNoteUnder(juce::Point<float> point) const
{
    const juce::Rectangle<float> staff = areas().staff.toFloat();
    if (!staff.contains(point))
    {
        return std::nullopt;
    }
    // Out to four ledger lines either side, as far as a click on the staff
    // can sensibly mean a note.
    const int steps = miniStaffStepsAt(staff, staffStyle(), point.y);
    if (std::abs(steps) > 12)
    {
        return std::nullopt;
    }
    return pitch::pitchAtStaffPosition(staffClef(), steps, 0);
}

void NoteViewComponent::updateHover(juce::Point<float> point)
{
    const std::optional<int> key = keyUnder(point);
    const std::optional<score::Pitch> note = key.has_value() ? std::nullopt : staffNoteUnder(point);
    setMouseCursor(
        key.has_value() || note.has_value() ? juce::MouseCursor::PointingHandCursor
                                            : juce::MouseCursor::NormalCursor);
    if (key != hoverKey || note != hoverStaffNote)
    {
        hoverKey = key;
        hoverStaffNote = note;
        repaint();
    }
}

void NoteViewComponent::mouseMove(const juce::MouseEvent& event)
{
    updateHover(event.position);
}

void NoteViewComponent::mouseExit(const juce::MouseEvent&)
{
    hoverKey.reset();
    hoverStaffNote.reset();
    repaint();
}

void NoteViewComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
    {
        return;
    }
    if (const std::optional<int> key = keyUnder(event.position))
    {
        // A key has no spelling, so it is spelled as the open score's key
        // would spell it: flats in a flat key, sharps otherwise.
        if (const auto spelled = pitch::spellForKey(*key, target.keyFifths))
        {
            target.publish(pitch::chosenTarget(*spelled, *spelled, "Chosen on the Note View"));
        }
    }
    else if (const std::optional<score::Pitch> note = staffNoteUnder(event.position))
    {
        target.publish(pitch::chosenTarget(*note, *note, "Chosen on the Note View"));
    }
    repaint();
}

void NoteViewComponent::paintReading(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const AppPalette colours = palette();
    juce::String text;
    if (currentNote.has_value())
    {
        const long cents = std::lround(currentCents);
        text = nameOfMidi(*currentNote, target.keyFifths) + "  " + (cents > 0 ? "+" : "") +
               juce::String(cents) + juce::String::fromUTF8(" \xC2\xA2");
    }
    else
    {
        text = l10n::text("notes.prompt");
    }
    graphics.setColour(colours.foreground);
    graphics.setFont(textFont(appearance::TextRole::heading, juce::Font::bold));
    graphics.drawText(
        text, area.removeFromLeft(area.getWidth() / 2), juce::Justification::centredLeft);

    graphics.setColour(colours.muted);
    graphics.setFont(textFont(appearance::TextRole::control));
    const auto aimingAt = target.target();
    graphics.drawText(
        aimingAt != nullptr
            ? l10n::format("target.heading", {{"note", l10n::noteName(aimingAt->name)}})
            : l10n::text(target.whyNone()),
        area, juce::Justification::centredRight, true);
}

void NoteViewComponent::paintStaff(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const AppPalette colours = palette();
    const auto aimingAt = target.target();
    const MiniStaffStyle style = staffStyle();

    std::vector<MiniStaffNote> heads;
    if (aimingAt != nullptr)
    {
        if (const auto spelled = pitch::parseNoteName(aimingAt->name))
        {
            heads.push_back({*spelled, targetHeadX(area), colours.accent, false});
        }
    }
    // Where a click would put the target, faintly, in its place.
    if (hoverStaffNote.has_value())
    {
        heads.push_back(
            {*hoverStaffNote, targetHeadX(area), colours.accent.withAlpha(0.4f), false});
    }
    if (currentNote.has_value())
    {
        if (const auto spelled = pitch::spellMidi(*currentNote, true))
        {
            const auto shade = zone();
            heads.push_back(
                {*spelled, sungHeadX(area),
                 shade.has_value() ? intonationZoneColour(*shade) : colours.foreground, true});
        }
    }

    drawMiniStaff(graphics, area.toFloat(), staffClef(), heads, style);
}

void NoteViewComponent::paintTrail(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const AppPalette colours = palette();
    graphics.setColour(colours.outline);
    graphics.drawRect(area, 1);

    // Faint guides down from each C, so the trail can be read against the keys.
    graphics.setColour(colours.outline.withAlpha(0.6f));
    for (int key = window.lowest; key <= window.highest; key += 12)
    {
        graphics.drawVerticalLine(
            static_cast<int>(xOf(key, area)), static_cast<float>(area.getY()),
            static_cast<float>(area.getBottom()));
    }

    // The acceptable range either side of the target, as an area coloured by
    // the zone the note being sung is in: green near perfect, blue inside the
    // range, amber near but off, red outside. Grey with nothing sung.
    const std::optional<pitch::IntonationZone> sungZone = zone();
    const std::optional<juce::Colour> sungColour =
        sungZone.has_value() ? std::optional<juce::Colour>(intonationZoneColour(*sungZone))
                             : std::nullopt;
    if (const auto aimingAt = target.target();
        aimingAt != nullptr && window.contains(aimingAt->midiNoteNumber))
    {
        const double centre = aimingAt->midiNoteNumber;
        const double half = toleranceCents() / 100.0;
        const float from = xOf(centre - half, area);
        const float to = xOf(centre + half, area);
        const juce::Rectangle<float> range(
            from, static_cast<float>(area.getY()), std::max(2.0f, to - from),
            static_cast<float>(area.getHeight()));
        const juce::Colour shade = sungColour.value_or(colours.muted);
        graphics.setColour(shade.withAlpha(sungColour.has_value() ? 0.22f : 0.12f));
        graphics.fillRect(range);
        graphics.setColour(shade.withAlpha(0.65f));
        graphics.drawVerticalLine(static_cast<int>(range.getX()), range.getY(), range.getBottom());
        graphics.drawVerticalLine(
            static_cast<int>(range.getRight()), range.getY(), range.getBottom());
    }

    // The target as a line down to its key.
    if (const auto aimingAt = target.target();
        aimingAt != nullptr && window.contains(aimingAt->midiNoteNumber))
    {
        graphics.setColour(colours.accent.withAlpha(0.7f));
        const float x = xOf(aimingAt->midiNoteNumber, area);
        const float dashes[] = {4.0f, 4.0f};
        graphics.drawDashedLine(
            juce::Line<float>(
                x, static_cast<float>(area.getY()), x, static_cast<float>(area.getBottom())),
            dashes, 2, 1.5f);
    }

    // Newest at the bottom, by the keys; older higher up.
    const auto& points = trail.recent();
    if (points.empty())
    {
        return;
    }
    const double now = points.back().seconds;
    juce::Path path;
    bool drawing = false;
    for (const notes::TrailPoint& point : points)
    {
        if (!point.midi.has_value() || !window.contains(static_cast<int>(std::lround(*point.midi))))
        {
            drawing = false;
            continue;
        }
        const float x = xOf(*point.midi, area);
        const float y = static_cast<float>(area.getBottom()) -
                        (static_cast<float>((now - point.seconds) / notes::trailSeconds) *
                         static_cast<float>(area.getHeight()));
        if (drawing)
        {
            path.lineTo(x, y);
        }
        else
        {
            path.startNewSubPath(x, y);
            drawing = true;
        }
    }
    graphics.setColour(sungColour.value_or(colours.foreground));
    graphics.strokePath(path, juce::PathStrokeType(2.5f));
}

void NoteViewComponent::paintKeyboard(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const AppPalette colours = palette();
    const auto aimingAt = target.target();
    const std::optional<int> targetKey =
        aimingAt != nullptr ? std::optional<int>(aimingAt->midiNoteNumber) : std::nullopt;

    const std::optional<pitch::IntonationZone> sungZone = zone();
    const auto keyFill = [&](int key, juce::Colour normal)
    {
        if (currentNote == key && sungZone.has_value())
        {
            return intonationZoneColour(*sungZone);
        }
        if (heldKeys.count(key) != 0)
        {
            return juce::Colour(heldColour);
        }
        // The key a click would choose.
        if (hoverKey == key)
        {
            return normal.interpolatedWith(colours.accent, 0.35f);
        }
        return normal;
    };

    const float whiteWidth =
        static_cast<float>(area.getWidth()) /
        static_cast<float>(
            notes::keyCentre(window.highest) - notes::keyCentre(window.lowest) + 1.0);
    const auto keyLeft = [&](int key, float width) { return xOf(key, area) - (width * 0.5f); };

    for (int key = window.lowest; key <= window.highest; ++key)
    {
        if (notes::isBlackKey(key))
        {
            continue;
        }
        const juce::Rectangle<float> rect(
            keyLeft(key, whiteWidth), static_cast<float>(area.getY()), whiteWidth,
            static_cast<float>(area.getHeight()));
        graphics.setColour(keyFill(key, juce::Colours::white));
        graphics.fillRect(rect);
        graphics.setColour(juce::Colours::black.withAlpha(0.5f));
        graphics.drawRect(rect, 1.0f);
        if (key % 12 == 0)
        {
            graphics.setColour(juce::Colours::black.withAlpha(0.6f));
            graphics.setFont(textFont(appearance::TextRole::fine));
            graphics.drawText(
                "C" + juce::String((key / 12) - 1),
                rect.withTrimmedTop(
                    rect.getHeight() - 14.0f * textSize(appearance::TextRole::fine) / 10.0f),
                juce::Justification::centred);
        }
    }
    const float blackWidth = whiteWidth * 0.6f;
    for (int key = window.lowest; key <= window.highest; ++key)
    {
        if (!notes::isBlackKey(key))
        {
            continue;
        }
        const juce::Rectangle<float> rect(
            keyLeft(key, blackWidth), static_cast<float>(area.getY()), blackWidth,
            static_cast<float>(area.getHeight()) * 0.62f);
        graphics.setColour(keyFill(key, juce::Colour(0xff222222)));
        graphics.fillRect(rect);
    }

    // The target: a ring at the foot of its key, in the accent.
    if (targetKey.has_value() && window.contains(*targetKey))
    {
        const bool black = notes::isBlackKey(*targetKey);
        const float x = xOf(*targetKey, area);
        const float y = static_cast<float>(area.getY()) +
                        (static_cast<float>(area.getHeight()) * (black ? 0.48f : 0.72f));
        const float size = std::min(whiteWidth * 0.6f, 14.0f);
        graphics.setColour(colours.accent);
        graphics.drawEllipse(x - (size * 0.5f), y - (size * 0.5f), size, size, 2.5f);
    }
}
