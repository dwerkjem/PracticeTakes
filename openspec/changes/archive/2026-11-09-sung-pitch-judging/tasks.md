## 1. Judging

- [x] 1.1 `PitchContour`: YIN over a take's audio, per-frame frequency and
      confidence. Tests: sine accuracy, a weak fundamental with a loud second
      harmonic, noise, silence; a benchmark.
- [x] 1.2 `Intonation`: a note's span and regions, sure frames, octave folding,
      median, spread, score and verdict. Tests: in tune, sharp, flat, a scoop,
      low confidence, an octave down.
- [x] 1.3 Matching and the recorder: a single sung note judged correct or wrong
      by its pitch; extra notes' pitch; `Take::referenceHz`;
      `TakeSources::referencePitch`; `TakeRecorder::setIntonation`. Tests.

## 2. The review

- [x] 2.1 Rows, summary, `ReviewMarks` and the Score View's sharp, flat and
      uncertain marks. Tests.
- [x] 2.2 `practice.inTuneCents` and the Settings → Practice slider, wired to
      the recorder. Tests.
- [x] 2.3 `syntheticSungTake`, the `take-review-sung` state and surface.
      Captures read.

## 3. Docs

- [x] 3.1 The wiki (Take Review, Settings), the Features row, ARCHITECTURE.md.
