## 1. Matching (`src/platform/evaluation/`, JUCE-free)

- [x] 1.1 `NoteMatching`: pitch-set comparison in matched moments; unmatched
      moments and onsets; no pitch; ambiguity; the report and its summary.
- [x] 1.2 Tests:
      - correct;
      - a wrong note in a chord;
      - a missed repeat;
      - a stray key;
      - an unmatched onset with keys;
      - a microphone take;
      - ambiguity;
      - a keyboard take end to end through `judgeNotes`.

## 2. Docs

- [x] 2.1 `ARCHITECTURE.md` § Evaluation.
