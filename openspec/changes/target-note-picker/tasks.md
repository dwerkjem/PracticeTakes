## 1. The model

- [x] 1.1 `TargetChoice.h` (JUCE-free): note names, staff positions, stepping
      by semitone and by key. Tests.
- [x] 1.2 `Target::origin`; the shell keeps a chosen target over a score when
      asked; the target and the setting saved; `ToolServices::target`
      writable. Tests.

## 2. The picker

- [x] 2.1 The picker call-out from the tuner (decision 1), with the steady-reading
      rule (decision 3).
- [x] 2.2 The Score View's right-click menu (decision 6).
- [ ] 2.3 The setting in Settings → Practice.

## 3. Verification and docs

- [ ] 3.1 An approved state with the picker open; captures read.
- [ ] 3.2 The wiki (the tuner's target, choosing it, the setting), Features
      row, ARCHITECTURE.md.
