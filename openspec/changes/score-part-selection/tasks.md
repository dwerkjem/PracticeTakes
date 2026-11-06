Ordered so each group can be built and tested before the next leans on it: the
layout filter and the codec are JUCE-free and come first; the shell's menu
contract next; the Score View last, where they meet.

## 1. The layout draws only the parts shown

- [ ] 1.1 `PartKey` (`id`, `name`) and `LayoutOptions::hiddenParts`, a list of
      them (design.md decision 3).
- [ ] 1.2 `layOutScore` skips a part whose id and name both match, where it
      builds `drawnParts` and before any notice about that part. If no
      drawable part would be left, the filter is ignored (decision 2).
- [ ] 1.3 Tests: a hidden part has no staves, names or glyphs, and the rest
      re-break as a score without it; a hidden part's notices are silent; the
      tempo mark is drawn above the new top part; hiding every part shows every
      part; a matching id with another name hides nothing; `partName` lists only
      the parts shown.

## 2. The selection is remembered

- [ ] 2.1 `score_view::Settings::hiddenParts`, and `hidden=` in the payload,
      with names percent-encoded for `:` `,` `;` `=` and `%` (decision 6).
      `settingsVersion` stays 1.
- [ ] 2.2 Tests: round trip, including a name holding each reserved character;
      an unreadable entry skipped and the rest kept; a payload from before this
      change decoding to nothing hidden.

## 3. The menu contract grows a submenu and a disabled entry

- [ ] 3.1 `ToolComponent::MenuEntry::enabled` (default true) and
      `MenuEntry::submenu` (default empty) (decision 4).
- [ ] 3.2 Depth-first numbering of an entry tree, in the JUCE-free workspace
      menu model, and a lookup from id back to the entry.
- [ ] 3.3 `ToolOptionsButton` renders submenus with `addSubMenu` and disabled
      entries as disabled, using that numbering.
- [ ] 3.4 Tests: a flat list numbers exactly as before; nested entries number
      depth-first and resolve back to their actions; a disabled entry is
      reported disabled.

## 4. The Score View

- [ ] 4.1 A "Parts" submenu when the open score has two or more drawable parts:
      one entry per part, named by `displayedPartName`, ticked when shown, and
      disabled when it is the last one shown (decision 5).
- [ ] 4.2 Toggling a part updates `Settings::hiddenParts`, relays out with
      `relayout(measureInView())`, and is saved through `captureSettings`
      (decision 7).
- [ ] 4.3 `showView` accepts a settings payload as well as a bare view name
      (decision 8).
- [ ] 4.4 The selection reaches the layout as `LayoutOptions::hiddenParts` on
      every relayout, and the layout's choice of drawn parts is what the menu's
      ticks show.

## 5. Verification and close-out

- [ ] 5.1 An approved state, `score-view-part-hidden`: the shipped sample in
      Page view with its voice hidden. A capture surface for it in
      `surfaces.py`, asking whether the piano alone fills each line, keeps its
      brace and name, and carries the tempo mark, and whether the title strip
      names only the piano. Pin the state in `ApprovedScoreFixturesTests`.
- [ ] 5.2 Behavioural questions on the surface: hide and show a part from the
      menu and keep the bar in view; the last part's entry cannot be chosen;
      the choice survives a restart.
- [ ] 5.3 Document the selection in `notation-engraving.md` (the layout's
      filter), and the menu contract in
      `docs/development/architecture/adding-a-tool.md`.
