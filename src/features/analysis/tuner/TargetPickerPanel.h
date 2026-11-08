#pragma once

#include <JuceHeader.h>

#include "../../../application/theme/Theme.h"
#include "../../../platform/pitch/CurrentTarget.h"
#include "../../../platform/pitch/TargetChoice.h"

#include <functional>
#include <optional>
#include <string>

// The target picker: every way of choosing the note to aim at, in a call-out
// from the tuner (openspec/changes/target-note-picker/design.md, decision 1).
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

        heading.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        addAndMakeVisible(heading);

        keyboardState.addListener(this);
        keyboard.setComponentID("target-keyboard");
        keyboard.setAvailableRange(24, 108);
        keyboard.setLowestVisibleKey(36);
        keyboard.setKeyWidth(14.0f);
        keyboard.setScrollButtonsVisible(true);
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
        nameEditor.setTextToShowWhenEmpty("F#4, Bb3...", colours.muted);
        nameEditor.onReturnKey = [this] { chooseByName(); };
        addAndMakeVisible(nameEditor);
        nameButton.onClick = [this] { chooseByName(); };
        addAndMakeVisible(nameButton);

        singButton.setComponentID("target-sing");
        singButton.setTooltip("Hold a note steadily, then take it as the target");
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
        semitoneUp.setTooltip("A semitone up");
        semitoneDown.setTooltip("A semitone down");
        keyUp.setTooltip("Up a step of the score's key");
        keyDown.setTooltip("Down a step of the score's key");

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
            graphics.setColour(ink.withAlpha(0.8f));
            for (const float middle : {trebleMiddle(), bassMiddle()})
            {
                for (int line = -2; line <= 2; ++line)
                {
                    const float y = middle - (static_cast<float>(line) * spacing);
                    graphics.drawHorizontalLine(static_cast<int>(y), 20.0f, getWidth() - 10.0f);
                }
            }
            graphics.setFont(juce::FontOptions(12.0f));
            graphics.drawText(
                "Treble", 0, static_cast<int>(trebleMiddle() - 30.0f), 60, 14,
                juce::Justification::left);
            graphics.drawText(
                "Bass", 0, static_cast<int>(bassMiddle() + 22.0f), 60, 14,
                juce::Justification::left);

            if (hover.has_value())
            {
                drawHead(graphics, *hover, ink.withAlpha(0.25f));
            }
            if (shown.has_value())
            {
                if (const auto place = placeOf(*shown))
                {
                    drawHead(graphics, *place, accent);
                }
            }
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

        // Where a pitch sits: on the treble staff from middle C up, else the
        // bass.
        [[nodiscard]] static std::optional<Place> placeOf(const score::Pitch& note)
        {
            const int letters = (note.octave * 7) + static_cast<int>(note.step);
            const bool treble = letters >= (4 * 7);
            const int middle = treble ? (4 * 7) + 6 : (3 * 7) + 1;
            const int steps = letters - middle;
            if (std::abs(steps) > 10)
            {
                return std::nullopt;
            }
            return Place{treble ? pitch::StaffClef::treble : pitch::StaffClef::bass, steps};
        }

        void drawHead(juce::Graphics& graphics, const Place& place, juce::Colour colour) const
        {
            const float middle =
                place.clef == pitch::StaffClef::treble ? trebleMiddle() : bassMiddle();
            const float y = middle - (static_cast<float>(place.steps) * spacing * 0.5f);
            const float x = static_cast<float>(getWidth()) * 0.5f;
            graphics.setColour(colour);
            graphics.fillEllipse(x - 6.0f, y - 4.5f, 12.0f, 9.0f);
            // Ledger lines beyond the staff.
            for (int step = 6; step <= std::abs(place.steps); step += 2)
            {
                const float ledger =
                    middle - (static_cast<float>(place.steps > 0 ? step : -step) * spacing * 0.5f);
                graphics.drawHorizontalLine(static_cast<int>(ledger), x - 10.0f, x + 10.0f);
            }
        }

        std::optional<Place> hover;
    };

    void handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float) override
    {
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
            nameEditor.setTextToShowWhenEmpty("Not a note name", juce::Colour(0xffd23c3c));
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
            current != nullptr ? juce::String::fromUTF8(("Target " + current->name).c_str())
                               : juce::String("No target: choose one"),
            juce::dontSendNotification);
        staff.shown = currentPitch();
        staff.ink = colours.foreground;
        staff.accent = colours.accent;
        staff.repaint();
    }

    pitch::CurrentTarget& target;
    std::function<std::optional<int>()> steadyNote;
    AppPalette colours;

    juce::Label heading;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard{
        keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};
    StaffPicker staff;
    juce::TextButton flat{juce::String::fromUTF8("\xE2\x99\xAD")};
    juce::TextButton natural{juce::String::fromUTF8("\xE2\x99\xAE")};
    juce::TextButton sharp{juce::String::fromUTF8("\xE2\x99\xAF")};
    juce::TextEditor nameEditor;
    juce::TextButton nameButton{"Set"};
    juce::TextButton singButton{"Use the note I'm on"};
    juce::TextButton semitoneDown{juce::String::fromUTF8("\xE2\x96\xBC semitone")};
    juce::TextButton semitoneUp{juce::String::fromUTF8("\xE2\x96\xB2 semitone")};
    juce::TextButton keyDown{juce::String::fromUTF8("\xE2\x96\xBC key step")};
    juce::TextButton keyUp{juce::String::fromUTF8("\xE2\x96\xB2 key step")};
};
