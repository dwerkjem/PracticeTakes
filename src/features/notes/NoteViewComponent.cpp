#include "NoteViewComponent.h"

#include "../../platform/score/PitchNames.h"

#include <cmath>

namespace
{
// Purple, so a held key is never read as one of the four zone colours.
constexpr juce::uint32 heldColour = 0xff9b6bd6;

[[nodiscard]] juce::String nameOfMidi(int midi)
{
    const auto spelled = pitch::spellMidi(midi, true);
    return spelled.has_value() ? juce::String(score::spelledName(*spelled)) : juce::String("--");
}
} // namespace

NoteViewComponent::NoteViewComponent(
    SharedPitchAnalysis& sharedPitchAnalysis,
    const pitch::ReferencePitch& applicationReferencePitch,
    const pitch::CurrentTarget& applicationTarget,
    MidiInputService& midiInputService,
    std::function<double()> toleranceSource)
    : pitchAnalysis(sharedPitchAnalysis), referencePitch(applicationReferencePitch),
      target(applicationTarget), midi(midiInputService), tolerance(std::move(toleranceSource))
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

double NoteViewComponent::toleranceCents() const
{
    return tolerance ? tolerance() : pitch::nearNoteCents / 2.0;
}

std::optional<pitch::IntonationZone> NoteViewComponent::zone() const
{
    const auto aimingAt = target.target();
    return notes::currentZone(
        currentPitch, currentCents,
        aimingAt != nullptr ? std::optional<int>(aimingAt->midiNoteNumber) : std::nullopt,
        toleranceCents());
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
    MenuEntry clefMenu{"Clef", {}};
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
        choice("Automatic", ClefChoice::automatic), choice("Treble", ClefChoice::treble),
        choice("Bass", ClefChoice::bass),
        choice("Treble, an octave down", ClefChoice::trebleOctaveDown)};
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

void NoteViewComponent::paint(juce::Graphics& graphics)
{
    const AppPalette colours = palette();
    graphics.fillAll(colours.background);

    auto bounds = getLocalBounds().reduced(12);
    paintReading(graphics, bounds.removeFromTop(30));
    bounds.removeFromTop(6);

    // Small and docked: the keyboard alone (design decision 5).
    const bool compact = getWidth() < 240 || getHeight() < 160;
    if (compact)
    {
        paintKeyboard(graphics, bounds);
        return;
    }

    // The staff to the left; the trail above the keys to the right, sharing
    // their width so a line in the trail sits over its key.
    paintStaff(graphics, bounds.removeFromLeft(std::min(160, bounds.getWidth() / 4)));
    bounds.removeFromLeft(10);
    const int keyboardHeight = std::min(90, std::max(48, bounds.getHeight() / 3));
    paintKeyboard(graphics, bounds.removeFromBottom(keyboardHeight));
    bounds.removeFromBottom(2);
    paintTrail(graphics, bounds);
}

void NoteViewComponent::paintReading(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const AppPalette colours = palette();
    juce::String text;
    if (currentNote.has_value())
    {
        const long cents = std::lround(currentCents);
        text = nameOfMidi(*currentNote) + "  " + (cents > 0 ? "+" : "") + juce::String(cents) +
               juce::String::fromUTF8(" \xC2\xA2");
    }
    else
    {
        text = "Sing or play a note";
    }
    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    graphics.drawText(
        text, area.removeFromLeft(area.getWidth() / 2), juce::Justification::centredLeft);

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(14.0f));
    const auto aimingAt = target.target();
    graphics.drawText(
        aimingAt != nullptr ? juce::String::fromUTF8(("Target " + aimingAt->name).c_str())
                            : juce::String::fromUTF8(target.whyNone().c_str()),
        area, juce::Justification::centredRight, true);
}

void NoteViewComponent::paintStaff(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    const AppPalette colours = palette();
    const auto aimingAt = target.target();
    const int reference = currentNote.value_or(aimingAt != nullptr ? aimingAt->midiNoteNumber : 60);
    const pitch::StaffClef staffClef = clefFor(reference);

    const float spacing = std::clamp(static_cast<float>(area.getHeight()) / 14.0f, 5.0f, 10.0f);
    const float middle = static_cast<float>(area.getCentreY());
    const float left = static_cast<float>(area.getX()) + 4.0f;
    const float right = static_cast<float>(area.getRight()) - 4.0f;

    graphics.setColour(colours.foreground.withAlpha(0.75f));
    for (int line = -2; line <= 2; ++line)
    {
        graphics.drawHorizontalLine(
            static_cast<int>(middle - (static_cast<float>(line) * spacing)), left, right);
    }
    graphics.setFont(juce::FontOptions(11.0f));
    graphics.setColour(colours.muted);
    const char* clefName = staffClef == pitch::StaffClef::treble ? "Treble"
                           : staffClef == pitch::StaffClef::bass ? "Bass"
                                                                 : "Treble 8vb";
    // Just above the staff, clear of any ledger lines a high note needs.
    graphics.drawText(
        clefName, juce::Rectangle<float>(left, middle - (spacing * 7.0f), right - left, 14.0f),
        juce::Justification::centredLeft);

    const auto head = [&](const score::Pitch& note, float x, juce::Colour colour, bool filled)
    {
        const int steps = pitch::staffStepsOf(note, staffClef);
        const float y = middle - (static_cast<float>(steps) * spacing * 0.5f);
        graphics.setColour(colours.foreground.withAlpha(0.75f));
        for (int ledger = 6; ledger <= std::abs(steps); ledger += 2)
        {
            const float ledgerY =
                middle - (static_cast<float>(steps > 0 ? ledger : -ledger) * spacing * 0.5f);
            graphics.drawHorizontalLine(static_cast<int>(ledgerY), x - spacing, x + spacing);
        }
        const juce::Rectangle<float> ellipse(
            x - (spacing * 0.65f), y - (spacing * 0.5f), spacing * 1.3f, spacing);
        graphics.setColour(colour);
        if (filled)
        {
            graphics.fillEllipse(ellipse);
        }
        else
        {
            graphics.drawEllipse(ellipse, 2.0f);
        }
        if (note.alter != 0)
        {
            graphics.setFont(juce::FontOptions(spacing * 1.6f));
            graphics.drawText(
                juce::String::fromUTF8(note.alter > 0 ? "\xE2\x99\xAF" : "\xE2\x99\xAD"),
                juce::Rectangle<float>(
                    x - (spacing * 2.6f), y - spacing, spacing * 1.6f, spacing * 2.0f),
                juce::Justification::centred);
        }
    };

    const float width = right - left;
    if (aimingAt != nullptr)
    {
        if (const auto spelled = pitch::parseNoteName(aimingAt->name))
        {
            head(*spelled, left + (width * 0.35f), colours.accent, false);
        }
    }
    if (currentNote.has_value())
    {
        if (const auto spelled = pitch::spellMidi(*currentNote, true))
        {
            const auto shade = zone();
            head(
                *spelled, left + (width * 0.7f),
                shade.has_value() ? intonationZoneColour(*shade) : colours.foreground, true);
        }
    }
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
            graphics.setFont(juce::FontOptions(10.0f));
            graphics.drawText(
                "C" + juce::String((key / 12) - 1), rect.withTrimmedTop(rect.getHeight() - 14.0f),
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
