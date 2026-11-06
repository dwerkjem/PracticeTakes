## MODIFIED Requirements

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
