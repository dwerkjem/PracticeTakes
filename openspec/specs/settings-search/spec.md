# settings-search Specification

## Purpose
TBD - created by archiving change everything-search. Update Purpose after archive.

## Requirements

### Requirement: Every Settings control can be found by its words
Each control in Settings SHALL be found by its label, by the words of its
tooltip, and by the names of its choices. Each Settings panel SHALL be found
by its name.

#### Scenario: By a choice
- **WHEN** the reader types "flats"
- **THEN** the Accidental preference setting is found

### Requirement: A chosen setting is shown
Choosing a setting SHALL open Settings on its panel, scroll the control into
view, and mark it for a moment.

#### Scenario: The Pitch bar
- **WHEN** the reader chooses the Pitch setting
- **THEN** Settings shows Practice with the Pitch row ringed
