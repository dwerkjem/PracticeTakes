## ADDED Requirements

### Requirement: A capture run can be narrowed to named surfaces
The capture pass SHALL accept a set of surfaces to cover, named by their
approved state, and SHALL capture only those. A name that no surface in the
run's mode offers SHALL be rejected before the run starts, naming what was
asked for and what is available. With no set given, the run SHALL cover every
surface its mode covers, as it does today.

Narrowing SHALL compose with the existing resolution and palette sets rather
than replacing them: a narrowed run still visits each chosen surface at every
configured resolution and in every configured palette.

#### Scenario: Capturing one surface
- **WHEN** a capture pass is asked for a single surface by name
- **THEN** only that surface is captured, at every configured resolution and
  palette, and no other surface is visited

#### Scenario: A misspelled surface name
- **WHEN** a capture pass is asked for a name no surface offers
- **THEN** the run fails before capturing anything, reporting the unknown name
  and the names that exist
- **AND** it does not silently complete having captured nothing

#### Scenario: No selection given
- **WHEN** a capture pass is run without naming any surface
- **THEN** it covers every surface its mode covers

### Requirement: A capture run can use a display of its own
The capture pass SHALL be able to run on a private display, so that no window
appears on the operator's screen and nothing takes focus while it runs. The
images produced SHALL be equivalent to those captured on the desktop display.

The private display SHALL be at least as large as the largest geometry the run
captures, so that a geometry defined in terms of the available screen is not
quietly reduced to a smaller one.

When a private display is requested and the mechanism providing it is not
installed, the run SHALL refuse with the command that installs it, and SHALL NOT
fall back to the desktop display — a fallback would put windows on the screen of
someone who asked for a private display precisely so that would not happen.

#### Scenario: Capturing while the machine is in use
- **WHEN** a capture pass runs on a private display
- **THEN** no application window appears on the operator's screen, focus is
  never taken, and every surface is captured as usual

#### Scenario: The display mechanism is absent
- **WHEN** a private display is requested on a machine without it installed
- **THEN** the run refuses, names the command that installs it, and captures
  nothing on the desktop display

#### Scenario: A geometry defined by the screen
- **WHEN** a run on a private display captures a surface at a geometry that
  asks for the whole available screen
- **THEN** the screen is large enough that the geometry differs from the
  ordinary window size

### Requirement: A capture run can be thrown away
The capture pass SHALL be able to write to a temporary store instead of the
verification history, for a capture taken only to be looked at once. Such a run
SHALL NOT appear in the history, SHALL NOT consume a run number in it, and SHALL
report where its images were written.

The images SHALL outlive the process that made them, since looking at them is
the reason the run happened; cleaning them up is left to the machine's temporary
directory rather than done on exit.

Asking for a throwaway run and naming a store, or asking for a throwaway run and
resuming an existing one, SHALL be refused rather than resolved silently: both
choose where the run lives, and a throwaway store has nothing to resume.

#### Scenario: A capture taken to answer one question
- **WHEN** a capture pass runs as a throwaway
- **THEN** its images are written somewhere temporary and reported
- **AND** the verification history contains no new run

#### Scenario: A throwaway run and a named store
- **WHEN** a throwaway run also names a store to write to
- **THEN** it is refused, because both choose where the run lives

#### Scenario: Resuming a throwaway run
- **WHEN** a throwaway run is asked to resume an existing run
- **THEN** it is refused, because a throwaway store is empty by construction

### Requirement: A capture run reports where its images were written
On completion the capture pass SHALL report the directory holding the images it
wrote, alongside the count captured and the way to open the review grid. Someone
who captured a small selection to inspect a change needs the files themselves,
not only a browser view of them.

#### Scenario: A completed run
- **WHEN** a capture pass finishes
- **THEN** the path to its images is printed with the summary
