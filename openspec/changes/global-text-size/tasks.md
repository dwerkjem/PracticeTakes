## 1. The rule, JUCE-free

- [ ] 1.1 `appearance::TextScale`: bounds, steps, snapping, scaled size rounded
      to half a point (decision 1). Tests.
- [ ] 1.2 The role table: each role's name and base size, matching today's
      sizes exactly (decision 2). Tests.

## 2. Storing it

- [ ] 2.1 `appearance.textScale` in the settings file and in the exported
      bundle's appearance (decision 5). Tests: a missing value, an
      out-of-range one, and a round trip.

## 3. Reaching every component

- [ ] 3.1 `AppLookAndFeel` applies the scale to widget fonts (decision 3).
- [ ] 3.2 `ToolComponent::setTextScale`, pushed by the shell like the theme.
- [ ] 3.3 Convert the shell and Settings window call sites to roles; their
      fixed heights follow the text (decision 4).
- [ ] 3.4 Convert each tool's call sites to roles, one commit per tool; their
      fixed heights and compact thresholds follow the text.

## 4. The setting

- [ ] 4.1 Text size in Settings → Appearance, with the preview of every role
      at the chosen size; changing it relays out every window at once.

## 5. Verification and docs

- [ ] 5.1 Full capture at 100%, compared with the approved references:
      nothing moves.
- [ ] 5.2 Approved states for 80% and 150%. Capture them, read every picture,
      and fix anything clipped.
- [ ] 5.3 The wiki (Workspace and Settings: Appearance), and
      ARCHITECTURE.md on how text size reaches a tool.
