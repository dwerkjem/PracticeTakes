## Decisions

### 1. The review lives with the takes

`TakeRecorder` holds the review: a kept take, a part index, and the
`NoteReport` from `evaluation::judgeNotes`. The report is computed once when the
review changes, not on every paint. The Score View and the Take Review tool
both read it through `ToolServices::takes` and listen for `takesChanged`. One
place owns "what is being reviewed", so the list and the notation can't
disagree.

### 2. The part a take is judged against, by default

When a take ends, the recorder judges it against every part of its score. It
reviews the part that scores best:
- twice its correct notes;
- plus its on-time attacks;
- ties go to the earlier part.

A keyboard take finds the part the pianist was playing. A sung take has no
pitches until #42, so it finds the part whose rhythm it followed. Either way
the reader can change it, and judging again is cheap. This avoids asking
"which part are you?" before every take.

### 3. The marks

These are drawn over the notation, after the cursor, so the glyphs are
untouched:

| Result | Mark |
|---|---|
| correct | a green ring |
| wrong | a red filled ellipse, with the played pitch's name above it |
| missed | a grey ring |
| pitch not judged | no pitch mark; the timing mark only |
| early | an orange arrow pointing left, beside the head |
| late | an orange arrow pointing right, beside the head |
| extra | a red ✕ above the top staff, at the extra note's time |

A note is found by its part (`System::staffParts`) and `NoteRef`, exactly
as the cursor finds its targets. An extra note's x comes from
`ScoreCursor::placeOfTick` at its tick. The marks show only when the reviewed
take's score is the one on screen, and **Show take review** (on by default)
is remembered per Score View.

### 4. The Take Review tool

The tool is single-instance and reads no audio.

- **Header:** the take ("Take 3 · stopped in bar 14 · 0:42"), the part, and a
  summary line ("21 correct · 2 wrong · 1 missed · 3 extra · 4 early · 2 late").
- **List:** one row per result in time order, extras included, built by the
  JUCE-free `reviewRows`. A row reads "Bar 3, beat 2 · G4 · wrong, played A4 ·
  40 ms late". Clicking a row seeks the practice clock to its tick, unless a
  take is recording.
- **Empty:** with no take, it says how to record one.

### 5. Keeping a take from elsewhere

`TakeRecorder::keep(shared_ptr<const Take>)` adds an ended take to the list,
assigns it an id, and reviews it as decision 2 says. The capture harness builds
a keyboard take from the open score, with known mistakes, and keeps it.

## Decided while building

### Judging runs on a worker thread

When a take ends, the recorder judges it on a worker thread (`std::async`), and
its timer takes the result. A keyboard take is judged in microseconds, but a
microphone take's onsets are not. Onset detection over a three-minute take
took 31 s in a Debug build and 2.7 s optimised. Two changes cut that:

- The FFT multiplies its complex numbers by hand, and magnitudes skip
  `std::hypot`. This brought it to 1.2 s optimised; the `[.benchmark]` case in
  `OnsetDetectorTests.cpp` tracks it.
- A take's onsets are found once (`onsetsOf`), not once per part.

1.2 s is still far too long for the message thread.

The worker reads only a kept take, which nothing changes again: the take, its
score and its table are all immutable. Only the newest request's result is
taken, and an older one is dropped when it finishes. A future made by
`std::async` waits when it is destroyed, so one is never destroyed before it is
ready, except in the recorder's destructor at shutdown.

While a different take is being judged, there is no review at all: the old
take's marks would be wrong for the new one. While only the part changes, the
old review stays until the new one arrives.
