# workspace-pane-sizing Specification

## Purpose

Defines the contract for how the docked workspace divides its available space
among tools instead of enforcing a fixed per-pane floor that can exceed the
window itself. Covers the shell's obligation to keep every docked tool fully
visible, the tool's own responsibility to offer a reduced presentation when its
pane is small, and the requirement that running out of room show up as smaller
panes rather than a tool silently placed off screen.

## Requirements
### Requirement: Every docked tool is on screen
A workspace SHALL lay out every docked tool within the window. No tool that the
user has opened SHALL be positioned wholly or partly outside the visible area,
at any window size the application permits.

Where the space available is less than every pane would prefer, the panes SHALL
share what there is. A pane's preferred size SHALL NOT be treated as a floor
below which the layout overflows instead.

#### Scenario: A third tool is docked
- **WHEN** three tools are docked in a window at its default size
- **THEN** all three are fully within the window

#### Scenario: The window is narrowed below what the panes prefer
- **WHEN** a window holding several docked tools is resized down to the smallest
  size the application permits
- **THEN** every tool remains fully within the window, each narrower than before
- **AND** none is clipped by the window edge

#### Scenario: Two layouts that differ must not render identically
- **WHEN** a workspace of three docked tools is captured at a size where an
  earlier build dropped one
- **THEN** the result differs from the same capture of a two-tool workspace

### Requirement: A tool is told how much room it has, and decides what to drop
A tool SHALL be able to present a reduced form of itself when its pane is too
small for its full presentation. What to drop SHALL be the tool's decision: the
shell SHALL provide the size and SHALL NOT decide which parts of a tool matter.

A tool that offers no reduced form SHALL remain legible by the shell's own
means, and SHALL NOT be silently clipped.

#### Scenario: A pane too small for the full presentation
- **WHEN** a tool's pane is narrower than its full presentation needs
- **THEN** the tool draws its reduced form rather than a clipped version of the
  full one

#### Scenario: The pane grows again
- **WHEN** a pane that was showing a reduced form is widened past the threshold
- **THEN** the tool returns to its full presentation without losing its analysis
  state

#### Scenario: A tool with nothing reduced to show
- **WHEN** a tool that declares no reduced form is given a small pane
- **THEN** it is still fully within its pane and no part of it is cut off

### Requirement: Running out of room is visible in the layout, not silent
A workspace that cannot give its tools their preferred size SHALL express that
by making them smaller, which the user can see. It SHALL NOT resolve the
shortfall by placing a tool where it cannot be seen.

#### Scenario: More tools than the window comfortably holds
- **WHEN** the number of docked tools exceeds what the window can show at
  preferred size
- **THEN** every tool is visibly narrower, and none is missing
