## 1. The reference pitch

- [x] 1.1 `pitch::ReferencePitch` and the conversions (JUCE-free); `PitchTracker`
      reads it from its settings. Tests.
- [x] 1.2 Owned by `MainComponent`, in `ToolServices`, set in Settings → Practice,
      saved locally; the tuner passes it on. Tests for the setting.

## 2. The target

- [x] 2.1 `TargetNoteSelection` moves to `src/platform/score/` and chooses only a
      monophonic part (decision 3). Tests.
- [x] 2.2 `pitch::CurrentTarget`, set by the shell from each score, in
      `ToolServices`.
- [x] 2.3 `describeAgainstTarget` (JUCE-free) and the tuner's target line. Tests.

## 3. The Target Note tool leaves

- [x] 3.1 Its component, factory and catalog entry go; its ids alias the tuner;
      states, `all-tools-docked`, surfaces and the tool count follow; a test that
      a workspace naming `target-note` opens a tuner. "Removing a tool" in
      `adding-a-tool.md`.

## 4. Verification and docs

- [x] 4.1 The tuner with a target from the sample song, at A440 and at A442;
      captures read.
- [ ] 4.2 The wiki (Tools: the tuner's target line; Settings: reference pitch;
      the Target Note section replaced), Features rows, README,
      `score-model-findings.md` findings 2 and 6 closed, ARCHITECTURE.md.
