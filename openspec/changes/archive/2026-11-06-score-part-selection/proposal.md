## Why

The Score View draws every part of a score, which is the right default and not
always what someone wants to read. A singer practising their line wants their
voice and maybe the piano, without the other three choir parts; a pianist
wants the accompaniment without the voice. Today there is no way to choose
(#277). It is also the "part and voice selection" item of #33, minus selecting
one voice within a shared staff, which stays there.

## What Changes

- **A "Parts" submenu in the Score View's options menu**, one ticked entry per
  part, named as the part is beside its staves. All are ticked by default.
- **Hidden parts are not drawn at all.** The layout is given the parts to leave
  out, so systems re-space and re-break as if the score had only the parts
  shown: hiding three choir parts brings more bars onto each line, rather than
  leaving gaps. Part names, braces, barlines between parts, the tempo mark drawn
  above the top part, pagination and the title strip's part list all follow the
  parts shown, because they already follow the parts drawn.
- **At least one part stays shown.** The toggle of the last part shown is
  disabled.
- **Remembered per Score View**, beside its view and size, by MusicXML part id
  *and* name. A saved selection applies to a part only when the open score has a
  part with both. Exporters number parts `P1`, `P2`… in every file, so an id
  alone would hide the second part of the next score opened.
- **Switching keeps your place**, as changing view or size already does.
- **The tool menu contract grows two things**: an entry can be disabled, and an
  entry can hold a submenu. `ToolComponent::MenuEntry` is the shell's contract
  with every tool, so this is the multi-layer part.
- **Photographed**: an approved state and a capture surface showing the shipped
  sample with its voice hidden.

### Non-goals

- Selecting one voice of several sharing a staff (soprano and alto on one
  staff). That stays in #33.
- Hiding individual staves of a part, such as a piano's left hand. A part is
  shown or hidden whole.
- Playback or any other tool following the selection. It is the Score View's
  own, like its view and size.

## Capabilities

### New Capabilities
- `score-part-selection`: choosing which parts of the open score the Score View
  draws, the layout drawing only those, and the per-instance memory of the
  choice.

### Modified Capabilities
- `score-view-modes`: the remembered settings grow the selection, and keeping
  the place covers a change of selection as well as of view or size.

## Impact

- `src/application/tools/ToolComponent.h` — `MenuEntry::enabled` and
  `MenuEntry::submenu`.
- `src/application/shell/ui/workspace/components/ToolOptionsButton.h` — renders
  a disabled entry and a submenu, numbering nested entries.
- `src/features/score/engraving/` — `LayoutOptions::hiddenParts`, applied where
  `layOutScore` chooses its drawn parts.
- `src/features/score/ScoreViewSettings.{h,cpp}` — the selection in the payload,
  JUCE-free.
- `src/features/score/ScoreViewComponent.{h,cpp}` — the submenu, applying the
  selection, keeping the place, and `showView` accepting a settings payload for
  the capture state.
- `ApprovedWindowStates` and `surfaces.py` — one state and one surface.
- No change to the score model, the importer, or the audio thread.
