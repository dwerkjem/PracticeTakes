## ADDED Requirements

### Requirement: The cursor shows where the practice clock is
While a score is open, the Score View SHALL draw a cursor across the system
holding the practice clock's position, at the x of that position. At an onset
the cursor SHALL be at that onset's notes. Between onsets it SHALL move
linearly from one onset to the next. The cursor SHALL be drawn in every view.

#### Scenario: The cursor reaches a note as it sounds
- **WHEN** the clock reaches the onset of a note in a shown part
- **THEN** the cursor is drawn at that note's x, on its system

#### Scenario: The cursor moves through a held note
- **WHEN** the clock is halfway through a half note followed by another note
- **THEN** the cursor is halfway between the two notes

### Requirement: Current and upcoming notes are marked
The Score View SHALL mark the notes sounding at the clock's position, and,
distinctly, the notes of the next onsets up to the look-ahead, in the parts it
shows. The look-ahead SHALL be settable from none to four onsets, two by
default. A note tied from an earlier note SHALL NOT be marked as upcoming. The
marks SHALL leave every notehead legible.

#### Scenario: Two onsets ahead
- **WHEN** the look-ahead is two and the clock is on a note
- **THEN** that note is marked as sounding and the next two onsets' notes as
  upcoming

#### Scenario: A hidden part is not marked
- **WHEN** a singer has hidden the piano part
- **THEN** only the voice's notes are marked

### Requirement: The view follows the clock without disorienting jumps
While the clock plays and following is on, the Score View SHALL keep the
cursor's system in view. It SHALL turn to the page holding the cursor in Page
view, step a system at a time in Vertical scroll, and move a screen at a time
in Horizontal scroll. It SHALL NOT scroll continuously.

#### Scenario: The page turns at the next page's first system
- **WHEN** the cursor moves from the last system of page 1 to the first of
  page 2
- **THEN** the view shows page 2

### Requirement: Scrolling by hand suspends following
Scrolling, turning a page or pressing a page key while the clock plays SHALL
suspend following. While it is suspended and the clock plays, a Follow control
SHALL be shown. Following SHALL resume when that control is pressed or the
clock starts playing again.

#### Scenario: Looking ahead without being pulled back
- **WHEN** the reader scrolls two pages ahead while the clock plays
- **THEN** the view stays where they scrolled, and a Follow control is shown

### Requirement: The clock can be moved from the score
Clicking on the music SHALL seek the practice clock to the onset at or before
the click's position, on the clicked system. Go to bar SHALL seek the clock to
the first tick of the bar with the given printed number, or, when no bar is
numbered so, of the bar at that position.

#### Scenario: Starting from a clicked note
- **WHEN** the reader clicks a note in bar 12 and presses Play
- **THEN** the clock plays from that note

### Requirement: Page view fits the width or the whole page
Page view SHALL offer two fits: the width of the panel, the default, and the
whole page in the panel. Both SHALL be remembered with the view and size.

#### Scenario: The whole page at once
- **WHEN** Fit page is chosen in a docked panel
- **THEN** the whole page is shown without scrolling
