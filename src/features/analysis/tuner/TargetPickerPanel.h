#pragma once

#include "../../../application/localisation/Localisation.h"
#include <JuceHeader.h>

#include "../../../application/theme/AppLookAndFeel.h"
#include "../../../application/theme/MiniStaff.h"
#include "../../../application/theme/Theme.h"
#include "../../../platform/pitch/CurrentTarget.h"
#include "../../../platform/pitch/TargetChoice.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// The target picker: every way of choosing the note to aim at, in a call-out
// from the tuner (openspec/changes/archive/2026-11-08-target-note-picker/design.md, decision 1).
//
// It keeps no target of its own: each control writes the application's
// target at once, and every tool reading it shows the new one.
class TargetPickerPanel final
    : public juce::Component,
      private juce::MidiKeyboardState::Listener,
      private juce::Timer
{
  public:
    static constexpr int panelWidth = 560;

    // `steadyNote` answers "the note being held steadily now, if any" for Use
    // the note I'm on (decision 3).
    TargetPickerPanel(
        pitch::CurrentTarget& applicationTarget,
        std::function<std::optional<int>()> steadyNoteSource,
        Theme theme)
        : target(applicationTarget), steadyNote(std::move(steadyNoteSource)),
          colours(appPaletteFor(theme))
    {
        setComponentID("target-picker");

        heading.setFont(appearance::labelFont(appearance::TextRole::subheading, juce::Font::bold));
        addAndMakeVisible(heading);

        keyboardState.addListener(this);
        keyboard.setComponentID("target-keyboard");
        keyboard.setAvailableRange(24, 108);
        keyboard.setLowestVisibleKey(36);
        keyboard.setKeyWidth(14.0f);
        keyboard.setScrollButtonsVisible(true);
        // Middle C is C4 here, as everywhere else in the application.
        keyboard.setOctaveForMiddleC(4);
        keyboard.setWantsKeyboardFocus(false);
        addAndMakeVisible(keyboard);

        staff.onChoose = [this](const score::Pitch& chosen)
        { choose(chosen, "Chosen on the staff"); };
        addAndMakeVisible(staff);

        int index = 0;
        for (auto* button : {&flat, &natural, &sharp})
        {
            button->setClickingTogglesState(true);
            button->setRadioGroupId(4210);
            button->setComponentID("target-accidental-" + juce::String(index++));
            button->onClick = [this] {
                staff.alter = flat.getToggleState() ? -1 : sharp.getToggleState() ? 1 : 0;
            };
            addAndMakeVisible(*button);
        }
        natural.setToggleState(true, juce::dontSendNotification);

        nameEditor.setComponentID("target-name");
        nameEditor.setTextToShowWhenEmpty(l10n::text("target.name.prompt"), colours.muted);
        nameEditor.onReturnKey = [this] { chooseByName(); };
        addAndMakeVisible(nameEditor);
        nameButton.onClick = [this] { chooseByName(); };
        addAndMakeVisible(nameButton);

        singButton.setComponentID("target-sing");
        singButton.setTooltip(l10n::text("target.sing.tooltip"));
        singButton.onClick = [this]
        {
            if (const std::optional<int> held = steadyNote ? steadyNote() : std::nullopt)
            {
                if (const auto sung = pitch::spellMidi(*held, true))
                {
                    choose(*sung, "Sung");
                }
            }
        };
        addAndMakeVisible(singButton);

        const auto step = [this](juce::TextButton& button, const char* id, auto move)
        {
            button.setComponentID(id);
            button.onClick = [this, move]
            {
                if (const auto current = currentPitch())
                {
                    if (const auto next = move(*current))
                    {
                        choose(*next, "Chosen");
                    }
                }
            };
            addAndMakeVisible(button);
        };
        step(
            semitoneUp, "target-semitone-up",
            [](const score::Pitch& p) { return pitch::stepBySemitone(p, 1); });
        step(
            semitoneDown, "target-semitone-down",
            [](const score::Pitch& p) { return pitch::stepBySemitone(p, -1); });
        step(
            keyUp, "target-key-up",
            [this](const score::Pitch& p) { return pitch::stepInKey(p, 1, target.keyFifths); });
        step(
            keyDown, "target-key-down",
            [this](const score::Pitch& p) { return pitch::stepInKey(p, -1, target.keyFifths); });
        semitoneUp.setTooltip(l10n::text("target.semitone-up.tooltip"));
        semitoneDown.setTooltip(l10n::text("target.semitone-down.tooltip"));
        keyUp.setTooltip(l10n::text("target.key-up.tooltip"));
        keyDown.setTooltip(l10n::text("target.key-down.tooltip"));

        refresh();
        startTimerHz(10);
        setSize(panelWidth, 360);
    }

    ~TargetPickerPanel() override
    {
        keyboardState.removeListener(this);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(colours.panel.withAlpha(0.0f));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);
        heading.setBounds(bounds.removeFromTop(26));
        bounds.removeFromTop(6);
        keyboard.setBounds(bounds.removeFromTop(70));
        bounds.removeFromTop(8);

        auto staffRow = bounds.removeFromTop(150);
        auto accidentals = staffRow.removeFromRight(44);
        for (auto* button : {&flat, &natural, &sharp})
        {
            button->setBounds(accidentals.removeFromTop(32).reduced(2));
        }
        staff.setBounds(staffRow);
        bounds.removeFromTop(10);

        auto nameRow = bounds.removeFromTop(30);
        nameEditor.setBounds(nameRow.removeFromLeft(120));
        nameRow.removeFromLeft(6);
        nameButton.setBounds(nameRow.removeFromLeft(56));
        nameRow.removeFromLeft(12);
        singButton.setBounds(nameRow.removeFromLeft(170));
        bounds.removeFromTop(8);

        auto stepRow = bounds.removeFromTop(30);
        for (auto* button : {&semitoneDown, &semitoneUp, &keyDown, &keyUp})
        {
            button->setBounds(stepRow.removeFromLeft(110));
            stepRow.removeFromLeft(6);
        }
    }

  private:
    // A grand staff to click: the position under the pointer, with the
    // accidental chosen beside it.
    class StaffPicker final : public juce::Component
    {
      public:
        std::function<void(const score::Pitch&)> onChoose;
        int alter = 0;
        std::optional<score::Pitch> shown;
        juce::Colour ink = juce::Colours::black;
        juce::Colour accent = juce::Colours::blue;

        static constexpr float spacing = 8.0f;

        void paint(juce::Graphics& graphics) override
        {
            // On each staff, the faint head under the pointer, then the
            // target's over it.
            const float x = static_cast<float>(getWidth()) * 0.5f;
            std::vector<MiniStaffNote> trebleHeads;
            std::vector<MiniStaffNote> bassHeads;
            const auto add =
                [&](pitch::StaffClef clef, const score::Pitch& note, juce::Colour colour)
            {
                (clef == pitch::StaffClef::treble ? trebleHeads : bassHeads)
                    .push_back({note, x, colour, true});
            };
            if (hover.has_value())
            {
                if (const auto under = pitch::pitchAtStaffPosition(hover->clef, hover->steps, 0))
                {
                    add(hover->clef, *under, ink.withAlpha(0.25f));
                }
            }
            if (shown.has_value())
            {
                if (const auto place = placeOf(*shown))
                {
                    add(place->clef, *shown, accent);
                }
            }

            // A fixed spacing, larger heads, and no accidental drawn: the
            // heading names the note, and the buttons beside the staff choose
            // the accidental for a click.
            MiniStaffStyle style;
            style.lines = ink.withAlpha(0.8f);
            style.minSpacing = spacing;
            style.maxSpacing = spacing;
            style.headWidth = 1.5f;
            style.headHeight = 1.125f;
            style.ledgerReach = 1.25f;
            style.ledgersInNoteColour = true;
            style.accidentals = false;
            drawMiniStaff(
                graphics, staffArea(trebleMiddle()), pitch::StaffClef::treble, trebleHeads, style);
            drawMiniStaff(
                graphics, staffArea(bassMiddle()), pitch::StaffClef::bass, bassHeads, style);

            // The grand staff is labelled in the margin, above the treble and
            // below the bass, rather than above each staff.
            graphics.setColour(style.lines);
            graphics.setFont(appearance::fontFor(*this, appearance::TextRole::small));
            graphics.drawText(
                miniStaffClefName(pitch::StaffClef::treble), 0,
                static_cast<int>(trebleMiddle() - 30.0f), 60, 14, juce::Justification::left);
            graphics.drawText(
                miniStaffClefName(pitch::StaffClef::bass), 0,
                static_cast<int>(bassMiddle() + 22.0f), 60, 14, juce::Justification::left);
        }

        void mouseMove(const juce::MouseEvent& event) override
        {
            hover = positionAt(event.position.y);
            repaint();
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            hover.reset();
            repaint();
        }

        void mouseUp(const juce::MouseEvent& event) override
        {
            if (const auto place = positionAt(event.position.y))
            {
                if (const auto chosen =
                        pitch::pitchAtStaffPosition(place->clef, place->steps, alter))
                {
                    if (onChoose)
                    {
                        onChoose(*chosen);
                    }
                }
            }
        }

      private:
        struct Place
        {
            pitch::StaffClef clef = pitch::StaffClef::treble;
            int steps = 0;
        };

        [[nodiscard]] float trebleMiddle() const
        {
            return static_cast<float>(getHeight()) * 0.3f;
        }

        [[nodiscard]] float bassMiddle() const
        {
            return static_cast<float>(getHeight()) * 0.72f;
        }

        [[nodiscard]] std::optional<Place> positionAt(float y) const
        {
            const bool treble = y < (trebleMiddle() + bassMiddle()) * 0.5f;
            const float middle = treble ? trebleMiddle() : bassMiddle();
            const int steps = juce::roundToInt((middle - y) / (spacing * 0.5f));
            if (std::abs(steps) > 10)
            {
                return std::nullopt;
            }
            return Place{treble ? pitch::StaffClef::treble : pitch::StaffClef::bass, steps};
        }

        // One staff of the pair, its middle line at `middle`: the lines run
        // from 20 pixels in to 10 from the right (the mini staff insets its
        // area by four), and the height gives the fixed spacing.
        [[nodiscard]] juce::Rectangle<float> staffArea(float middle) const
        {
            constexpr float height = spacing * 14.0f;
            return {
                16.0f, middle - (height * 0.5f), static_cast<float>(getWidth()) - 22.0f, height};
        }

        // Where a pitch sits: on the treble staff from middle C up, else the
        // bass.
        [[nodiscard]] static std::optional<Place> placeOf(const score::Pitch& note)
        {
            const bool treble = (note.octave * 7) + static_cast<int>(note.step) >= (4 * 7);
            const pitch::StaffClef clef =
                treble ? pitch::StaffClef::treble : pitch::StaffClef::bass;
            const int steps = pitch::staffStepsOf(note, clef);
            if (std::abs(steps) > 10)
            {
                return std::nullopt;
            }
            return Place{clef, steps};
        }

        std::optional<Place> hover;
    };

    void handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float) override
    {
        if (showingTarget)
        {
            return;
        }
        // A key has no spelling, so it is spelled with a sharp.
        if (const auto key = pitch::spellMidi(midiNoteNumber, true))
        {
            choose(*key, "Chosen on the keyboard");
        }
    }

    void handleNoteOff(juce::MidiKeyboardState*, int, int, float) override {}

    void timerCallback() override
    {
        singButton.setEnabled(steadyNote && steadyNote().has_value());
        showTargetOnKeyboard();
    }

    void chooseByName()
    {
        if (const auto named = pitch::parseNoteName(nameEditor.getText().toStdString()))
        {
            choose(*named, "Chosen by name");
            nameEditor.clear();
        }
        else
        {
            nameEditor.setText({}, false);
            nameEditor.setTextToShowWhenEmpty(
                l10n::text("target.name.invalid"), juce::Colour(0xffd23c3c));
        }
    }

    void choose(const score::Pitch& chosen, const std::string& source)
    {
        target.publish(pitch::chosenTarget(chosen, chosen, source));
        refresh();
    }

    [[nodiscard]] std::optional<score::Pitch> currentPitch() const
    {
        const auto current = target.target();
        return current != nullptr ? pitch::parseNoteName(current->name) : std::nullopt;
    }

    void refresh()
    {
        const auto current = target.target();
        heading.setText(
            current != nullptr
                ? l10n::format(
                      "target.heading", {{"note", juce::String::fromUTF8(current->name.c_str())}})
                : l10n::text("target.heading.none"),
            juce::dontSendNotification);
        staff.shown = currentPitch();
        // A call-out is dark in either theme, so the staff is drawn light.
        staff.ink = juce::Colours::white.withAlpha(0.85f);
        staff.accent = colours.accent.brighter(0.3f);
        staff.repaint();
        showTargetOnKeyboard();
    }

    // The target's key held down on the keyboard, and no other; put back after
    // a click lets go of the key it pressed.
    void showTargetOnKeyboard()
    {
        const auto current = target.target();
        const int key = current != nullptr ? current->midiNoteNumber : -1;
        const juce::ScopedValueSetter<bool> quiet(showingTarget, true);
        for (int note = 0; note < 128; ++note)
        {
            const bool on = keyboardState.isNoteOn(1, note);
            if (note == key && !on)
            {
                keyboardState.noteOn(1, note, 1.0f);
            }
            else if (note != key && on && !keyboard.isMouseButtonDownAnywhere())
            {
                keyboardState.noteOff(1, note, 0.0f);
            }
        }
    }

    pitch::CurrentTarget& target;
    std::function<std::optional<int>()> steadyNote;
    AppPalette colours;
    bool showingTarget = false;

    juce::Label heading;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard{
        keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};
    StaffPicker staff;
    juce::TextButton flat{juce::String::fromUTF8("\xE2\x99\xAD")};
    juce::TextButton natural{juce::String::fromUTF8("\xE2\x99\xAE")};
    juce::TextButton sharp{juce::String::fromUTF8("\xE2\x99\xAF")};
    juce::TextEditor nameEditor;
    juce::TextButton nameButton{l10n::text("target.name.set")};
    juce::TextButton singButton{l10n::text("target.sing")};
    juce::TextButton semitoneDown{l10n::text("target.semitone-down")};
    juce::TextButton semitoneUp{l10n::text("target.semitone-up")};
    juce::TextButton keyDown{l10n::text("target.key-down")};
    juce::TextButton keyUp{l10n::text("target.key-up")};
};
