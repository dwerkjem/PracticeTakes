## Context

What is on `main` after `score-view-modes` and `engraving-polish`, surveyed for
this design:

- **`layOutScore` already chooses what it draws.** It walks
  `score->parts` and builds `drawnParts`, leaving out a part whose staves have no
  pitches or that holds no events, with a notice for each. Everything downstream
  reads `drawnParts`: staff groups and braces, barlines that break between
  parts, part names, `liftTempoMarksToTop`, the "bars only some parts have"
  count, `ScoreLayout::partName` (which the title strip shows) and pagination,
  which works on the systems the layout produced. A filter at that one place is
  a filter on all of them.
- **The Score View remembers itself** through `score_view::Settings`, encoded as
  `view=page;size=7` at `settingsVersion` 1. `decode` ignores keys it does not
  know, and discards the whole payload on a version mismatch.
- **The options menu is a flat list.** `ToolComponent::MenuEntry` is a label,
  an action and a tick. `ToolOptionsButton` numbers the tool's entries from a
  base id and adds the shell's own items after a separator. There is no
  submenu and no disabled entry.
- **Keeping the place** is `relayout(measureInView())`, which view and size
  changes already use.
- **The capture harness reaches a tool's view** through
  `ApprovedWindowState::toolView`, passed to `ToolComponent::showView(name)`;
  the Score View maps a bare view name to a fresh `Settings`.
- **Part ids repeat across files.** MusicXML requires an id per part, and
  exporters write `P1`, `P2`... in every file. The Score View's settings outlive
  the score open when they were saved: open a different score and the same
  Score View draws it.

## Goals / Non-Goals

**Goals**

- A part can be hidden and shown again from the Score View, and the layout then
  behaves as if the score had only the parts shown.
- At least one part is always shown.
- The choice survives a restart, and does not hide parts of a different score
  that happen to share an id.
- Testable without a display: the filter in the layout, the payload in the
  codec, the menu numbering in the shell's model.

**Non-Goals**

- Voices within a staff (#33), single staves of a part, or other tools
  following the selection.

## Decisions

### 1. Remember what is hidden, not what is shown

A selection is saved as the parts the user turned off. A part the user has
never seen -- added to the file since, or in a score opened later -- is shown,
which is the safe default: a part missing from the page is a part the reader
does not know to look for. A list of shown parts would hide every new part by
default.

### 2. A part is named by its id and its name together

A hidden entry is the pair (`Part::id`, `Part::name`), and it hides a part only
when the open score has a part with both. Re-importing the same file keeps both
the same, so the selection survives. A different score's `P2` is almost never
also called "Alto", so it is not hidden by accident. An id alone fails the
second test, which is the common case. A name alone fails when two parts share
one, as "Violin" can.

If, after matching, every drawable part would be hidden, the selection is
ignored and every part is shown. That covers a saved selection from another
score, and a hand-edited settings file.

### 3. The layout takes the filter; the Score View owns the choice

`LayoutOptions::hiddenParts` holds the pairs. `layOutScore` skips a matching
part where it builds `drawnParts`, before any notice about that part: a hidden
part says nothing about its percussion staff or its missing notes, because the
reader asked not to see it. Everything listed in Context then follows without
being touched. The layout stays a function of the score and its options, so
its tests need no Score View.

### 4. The menu contract grows a submenu and a disabled entry

`MenuEntry` gains `bool enabled = true` and `std::vector<MenuEntry> submenu`.
An entry with a submenu is a heading and its own action is unused.
`ToolOptionsButton` renders them with `PopupMenu::addSubMenu` and
`addItem(..., enabled, ticked)`, and numbers entries depth-first from its base
id, so a nested entry's action is found the way a flat one's is. Every other
tool's entries are flat and enabled, and are unaffected.

The numbering is a pure function of the entry tree, so it lives in the
JUCE-free workspace menu model beside the existing numbering, with tests there.

### 5. The submenu appears only when there is a choice

With one drawable part there is nothing to choose, so there is no "Parts"
entry. With two or more, the heading is "Parts", followed by one entry per part
in score order. Each is named by `displayedPartName`, as the part is labelled
beside its staves, and ticked when shown. The last shown part's entry is
disabled.

### 6. The payload adds a key; the version stays 1

`hidden=P2:Alto,P3:Tenor`. Ids are XML NCNames, so they never hold `:` `,`
`;` or `=`. Names can hold any of them, so each name is percent-encoded for
those four characters and `%`. `decode` already ignores keys it does not know,
so a version without this change reads the rest of a new payload. Bumping the
version would instead reset every saved Score View to defaults. An unreadable
entry is skipped, not fatal, as for the other keys.

### 7. Changing the selection keeps the place

Toggling a part calls `relayout(measureInView())`, the same path as a view or
size change. The bar in view stays in view, even though a hidden part moves
every system break.

### 8. The capture state passes a payload

`ScoreViewComponent::showView` accepts either a bare view name, as now, or a
full settings payload: anything containing `=`. That lets an approved state
name the sample with its voice hidden, without a new field on
`ApprovedWindowState`. The size is still reset to the default, as `showView`
already does, so a state photographs one way.

## Verification

- **Layout tests**, JUCE-free: a hidden part has no staves or names, and the
  rest re-break as a score without it; a hidden part's notices are silent; the
  tempo mark moves to the new top part; hiding every part shows every part; an
  id that matches with the wrong name hides nothing.
- **Codec tests**: round trip with names holding each reserved character;
  unknown and malformed entries; an old payload decodes to nothing hidden.
- **Menu model tests**: depth-first numbering, and a disabled entry that cannot
  be chosen.
- **A capture surface**, `score-view-part-hidden`: the shipped sample with the
  voice hidden, asking whether the piano alone fills the lines, carries the
  name and tempo, and keeps its brace.

## Risks

- **A shell contract change touches every tool's menu.** Mitigated by defaults:
  an entry that sets neither new field behaves exactly as before, and the menu
  model's tests pin the flat case.
- **A name change in the file un-hides a part.** Accepted: renaming a part in
  a notation editor is rare, and showing a part by mistake costs less than
  hiding one.
