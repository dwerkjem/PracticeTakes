# score-view-modes Specification

## Purpose
The Score View's three ways of laying out a score -- Page, Vertical scroll and
Horizontal scroll -- its enlarge and shrink control, and the per-instance memory
of both, with the bar in view kept in view across a change.
## Requirements
### Requirement: The Score View offers three views
The Score View SHALL offer three views from its options menu — Page, Vertical
scroll and Horizontal scroll — and SHALL open a new Score View in Page view. The
view SHALL change only how the laid-out systems are arranged and which width
the layout is given, never the engraving rules themselves.

#### Scenario: A new Score View opens in Page view
- **WHEN** a Score View is opened for the first time
- **THEN** the options menu shows Page, Vertical scroll and Horizontal scroll,
  with Page selected

#### Scenario: Vertical scroll is the view as it was
- **WHEN** Vertical scroll is chosen
- **THEN** the systems are laid out at the panel's width and stacked in one
  column with a vertical scrollbar, as before this change

### Requirement: Page view shows the score page by page
In Page view the Score View SHALL show one fixed-proportion page at a time, as
wide as the panel and scrolled within when taller than it, with a visible page
edge, and SHALL never split a system across pages. It SHALL turn
pages by on-screen buttons and by the Page Up, Page Down, Left, Right, Home and
End keys, and SHALL show which page of how many is in view.

#### Scenario: A score longer than a page
- **WHEN** a score whose systems do not fit one page is shown in Page view
- **THEN** the first page shows as many whole systems as fit, "Page 1 of n" is
  shown, and turning the page shows the next systems

#### Scenario: No bar split across pages
- **WHEN** any score is shown in Page view at any size
- **THEN** every system is drawn whole on exactly one page

#### Scenario: A system taller than a page
- **WHEN** one system is taller than the page
- **THEN** it is given a page of its own and the view reports that it is cut off,
  rather than shrinking the music

### Requirement: Horizontal scroll shows every bar on one system
In Horizontal scroll the Score View SHALL lay the whole score out on a single
system and SHALL scroll it horizontally, showing a vertical scrollbar only when
that system is taller than the panel.

#### Scenario: A multi-system score in Horizontal scroll
- **WHEN** a score that breaks into several systems in Vertical scroll is shown
  in Horizontal scroll
- **THEN** every bar is on one system and the view scrolls left to right

### Requirement: The music can be enlarged and shrunk in place
The Score View SHALL provide enlarge and shrink controls on the view itself,
stepping through a fixed set of staff sizes that includes the three sizes it
offered before, with keyboard shortcuts and a way back to the default size.

#### Scenario: Enlarging
- **WHEN** the enlarge button, or Ctrl/Cmd and +, is used
- **THEN** the staff is drawn one step larger and the layout reflows

#### Scenario: At the largest size
- **WHEN** the staff is at the largest size
- **THEN** the enlarge control is disabled and enlarging does nothing

#### Scenario: Back to the default
- **WHEN** Ctrl/Cmd and 0 is pressed, or Default size is chosen from the menu
- **THEN** the staff returns to the default size

### Requirement: The view and size are remembered per Score View
Each Score View instance SHALL save its view, its staff size and the parts it
hides, and SHALL restore them when the application restarts, independently of
any other Score View.

#### Scenario: Restart
- **WHEN** a Score View is set to Horizontal scroll at a larger size and the
  application is restarted
- **THEN** that Score View opens in Horizontal scroll at that size

#### Scenario: A saved size that no longer exists
- **WHEN** a saved staff size is not one of the sizes offered
- **THEN** the nearest offered size is used

#### Scenario: Restart with a part hidden
- **WHEN** a Score View hides the voice of the open score and the application is
  restarted with the same score
- **THEN** that Score View still hides the voice

### Requirement: Changing view or size keeps the place
When the view, the staff size or the parts shown change, the Score View SHALL
keep the bar that was in view before the change in view after it.

#### Scenario: Switching from Vertical to Page
- **WHEN** bar 30 is at the top of Vertical scroll and Page view is chosen
- **THEN** the page holding bar 30 is shown

#### Scenario: Enlarging in Horizontal scroll
- **WHEN** bar 12 is at the left edge of Horizontal scroll and the staff is
  enlarged
- **THEN** bar 12 is still in view

#### Scenario: Hiding a part
- **WHEN** bar 20 is in view and a part is hidden
- **THEN** bar 20 is still in view
