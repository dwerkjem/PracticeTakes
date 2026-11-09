## Context

Timing (`OnsetTiming`) matches each written moment to an onset. Matching
(`NoteMatching`) then compares the pitches inside a matched moment, and a
microphone onset brings none. This change gives a single sung note a pitch, by
reading the audio between its onset and the next, and judges it against the
written note at the take's reference.

## Decisions

### 1. A contour of the take, once

`evaluation::pitchContourOf(audio, count, sampleRate, startFrame, settings)`
returns one `ContourFrame` per hop:
- `frame`, the clock frame at the window's centre;
- `hertz`, which is 0 when unvoiced;
- `confidence`, 0–1;
- `rms`.

The tracker is YIN:
- the difference function comes from the autocorrelation, through the
  evaluation FFT;
- it is cumulative-mean normalised;
- it takes the first dip below an absolute threshold;
- the dip is refined by a parabola.

`confidence` is one minus the normalised difference at the chosen lag.

**YIN, not the tuner's detector.** The live `PitchDetector` takes the first
autocorrelation peak above a correlation score. YIN's cumulative-mean
normalisation is the classic answer to octave errors: a lag of half the period
(the octave above) never dips as low as the period itself unless the
fundamental is truly absent, and taking the *first* dip under the threshold
refuses the subharmonic below. The tuner's detector also depends on JUCE's FFT,
and evaluation stays JUCE-free.

The window is the power of two nearest 46 ms (2,048 samples at 44.1 kHz), so a
60 Hz fundamental fits two periods. The hop is a quarter of the window, about
12 ms. The range is 60–1,500 Hz: a bass's low E to above a soprano's high C.
The contour is found once per take, like the onsets, and shared by every part
`bestPartFor` tries.

### 2. A note's span and its regions

A sung note runs from its matched onset to the earlier of two points:
- the next moment's onset (matched, or else expected);
- its own written end, plus latency.

Its frames are split by time:
- **Attack:** the first quarter, at least 40 ms and at most 150 ms. A scoop or a
  consonant lives here.
- **Release:** the last sixth, at most 100 ms. Here the singer is already
  leaving for the next note.
- **Sustain:** the rest. A note too short to have a sustain uses its middle
  half instead.

Only sustain frames decide the verdict. Every frame is kept in the result with
its region, so a later view can draw the whole note.

### 3. Sure frames, folded octaves, the median

- **A sure frame** is voiced, with a confidence of at least 0.8.
- **Folding.** A sure frame's deviation from the written pitch, in cents at the
  take's reference, is folded by whole octaves into −600…+600 ¢. So an octave
  error by the tracker, or a tenor reading a treble line an octave down, does
  not turn a good note into a wrong one. The commonest octave among the sure
  sustain frames is recorded, and the review says "an octave low".
- **The note's deviation** is the median of the folded sure sustain frames. A
  passing transition cannot move a median the way it moves a mean. Its spread
  is the median absolute deviation about that.
- **The note's score**, 0–1, is the share of sure sustain frames within the
  tolerance. A steady in-tune note scores near 1; a wobbling one lower, even
  when its median is in tune.

### 4. The verdict

| Condition | Verdict | Outcome |
|---|---|---|
| Fewer than 4 sure sustain frames, or under 40% of them sure | uncertain | `pitchNotJudged` |
| \|deviation\| ≤ tolerance | in tune | `correct` |
| tolerance < \|deviation\| ≤ 50 ¢ | sharp or flat | `correct` |
| \|deviation\| > 50 ¢ | — | `wrong`, sung pitch = the nearest note |

- **Uncertain is not wrong.** It stays `pitchNotJudged`, which the summary has
  always shown as "played", and it now carries the reason.
- **Sharp and flat are still the right note.** They count as correct for
  matching and for choosing the best part. They are a mistake in the review
  list, because they are what a singer looks for.
- **The default tolerance is 25 ¢**, a quarter of a semitone: the usual
  classroom figure for "in tune" in a sung line. The tuner's ±5 ¢ is for
  holding one note on a meter, not for a phrase. The setting runs from 5 to
  50 ¢.

### 5. Only single notes

A moment with more than one written note in the part (a chord, or two voices
together) stays `pitchNotJudged`: one contour hears one pitch. A piano take
through the microphone therefore has its single-note lines judged and its
chords not, which is honest.

### 6. Extra notes

An extra microphone onset takes the median of the sure frames from 40 ms to
200 ms after it, given at least 4 of them, rounded to the nearest note. Without
those, its pitch stays unknown, as now.

### 7. The reference and the tolerance

- `Take::referenceHz` is the application's reference when the take started,
  taken from a new `TakeSources::referencePitch`. A take sung at A442 is judged
  at A442 even after the reference changes.
- `IntonationSettings::toleranceCents` comes from `practice.inTuneCents`.
  `TakeRecorder::setIntonation` keeps it, and re-judges the take under review
  when it changes, which takes about a second of worker-thread time.

### 8. The review

- **Rows:**
  - "G4 · 18 ¢ sharp · on time";
  - "G4 · in tune";
  - "G4 · wrong, sang A4";
  - "G4 · pitch uncertain";
  - "an octave low" is appended where it applies.
  - Sharp and flat rows are mistakes.
- **Summary:** for a take with any intonation, the correct notes are shown as
  "N in tune", followed by "N sharp · N flat", and uncertain notes are counted
  as "uncertain" rather than "played".
- **Score View:**
  - an in-tune note is the green ring;
  - a sharp or flat note is an amber ring with a small arrow above (sharp) or
    below (flat) the head, upright where the timing arrows lie sideways;
  - an uncertain note is a faint ring.

### 9. Testing and the capture

- `testcontrol::syntheticSungTake` sings a part's single notes with a harmonic
  tone, plus a short scoop into each. A fixed pattern makes some notes sharp,
  some flat, one wrong, one too quiet to be sure of, and one missed.
- The unit tests sing:
  - a sine and a harmonic-rich tone whose second harmonic is louder than the
    fundamental;
  - noise;
  - a held note with a scoop;
  - steady sharp and flat notes.
- The approved state `take-review-sung` reviews that take on the sample score's
  voice part.

## Risks

- **Vibrato.** A wide vibrato is symmetric, so the median survives it, but the
  score (decision 3) drops. A wide vibrato reads as a less steady note, which
  is arguably right.
- **The speed of the contour.** 15,500 windows for three minutes: about one
  second optimised (measured at -O2), as the onsets take. In a Debug build,
  twenty seconds of singing takes 4.4 s to judge, so the `take-review-sung`
  surface warms up for ten seconds. The `[.benchmark]` case pins it, and it
  runs on the judging thread.
- **Latency.** The device's stated latency is used until #38 calibrates it. A
  span shifted by a few tens of milliseconds moves frames between attack and
  sustain, not out of the note.

## Decided while building

- **The contour's transform is the window's length, not twice it.** Lags run
  only to half the window, and the integration covers the first half, so no
  product wraps around a 2,048-point transform. The two real inputs (the first
  half and the whole window) are packed into one complex transform, so each
  frame costs one forward and one inverse FFT.
- **A note's span uses the uncorrected onset frame.** The contour is on the
  audio's own timeline, where the onset is. Only the written end and an
  unmatched next moment have the take's latency added, as timing does.
- **The synthetic sung take leads in with half a second of silence**, as a
  count-in would. The onset detector needs something before the first note to
  find it against.
- **An uncertain sung note's row is muted** in the Take Review, like a missed
  note's but without being a mistake.
- **The capture sings the score's first part that plays one note at a time**
  (`isMonophonic`, in `platform/score`), so it is the sample's voice and not its piano.
