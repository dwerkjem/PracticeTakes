# text-size Specification

## Purpose
One text size for the whole application: its range, the preview of every size
it changes, what it scales and what it leaves to size itself, and how it is
remembered.
## Requirements
### Requirement: One text size scales the application's UI text
Settings → Appearance SHALL offer a text size from 80% to 150% in 10% steps,
100% by default. Every menu, button, label, tool heading, axis or tick label,
readout and piece of small print SHALL be drawn at its base size times the text
size. Text that sizes itself to its pane (the tuner's note name and its sharp
and flat signs, and the Score View's notation) SHALL NOT change with it.

#### Scenario: Larger text
- **WHEN** the text size is set to 150%
- **THEN** a label whose base size is 12 points is drawn at 18 points, and the
  tuner's note name is the size it was

#### Scenario: Unchanged at 100%
- **WHEN** the text size is 100%
- **THEN** every surface looks as it did before this change

### Requirement: The setting shows every text size it changes
The text size setting SHALL show an example of each text role the application
uses, named, at the chosen size and with its base size, and SHALL update the
examples as the setting changes.

#### Scenario: Choosing a size
- **WHEN** the user moves the text size from 100% to 120%
- **THEN** every example under it grows to 120% of its base size at once

### Requirement: Nothing is clipped at any text size
At every text size, text in the main window, the Settings window and every tool
SHALL fit in its row, button or panel without being clipped or overlapping
other text. A pane too small for its text SHALL use its compact form.

#### Scenario: The largest size
- **WHEN** the text size is 150% and every tool is open at the default window
  size
- **THEN** no label, button or menu text is clipped

### Requirement: The text size is remembered
The text size SHALL be saved with the settings and restored on launch. It SHALL
travel with appearance in an exported settings bundle. A missing or
out-of-range value SHALL load as 100%.

#### Scenario: A settings file from 0.7.0
- **WHEN** the application loads a settings file written before this change
- **THEN** the text size is 100%
