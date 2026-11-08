## 1. Expected and performed (`src/platform/evaluation/`, JUCE-free)

- [x] 1.1 `ExpectedAttacks`: moments per tick, ties, rests, grace notes,
      chords and voices merged. Tests.
- [x] 1.2 `KeyboardOnsets`: note-ons gathered into chords. Tests.
- [x] 1.3 `Fft` and `OnsetDetector`: spectral flux and peak picking. Tests on
      synthetic attacks (clicks, plucked tones, a chord, silence and noise),
      placed within 10 ms.

## 2. Alignment and judgement

- [ ] 2.1 `OnsetAligner`: dynamic programming within windows. Tests for a
      missed note, an extra onset, repeated pitches, and a uniform delay.
- [ ] 2.2 `TimingJudgement`: tolerance, verdicts, milliseconds and beats,
      latency correction, uncertainty, and the summary. Tests, including a
      tempo change.
- [ ] 2.3 `judgeTake`: a take's report from the keyboard or the microphone.
      Tests on synthetic takes.

## 3. Docs

- [ ] 3.1 `ARCHITECTURE.md` (evaluation) and the agent guide's source map. The
      Features row stays MVP until #52 shows the results.
