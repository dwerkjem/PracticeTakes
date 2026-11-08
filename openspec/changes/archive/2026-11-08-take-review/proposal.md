## Why

A take can now be recorded (#40), timed (#43) and matched note by note (#48),
but nobody can see any of it. #52 is where the practice loop shows its
result: on the score, every note right or wrong, missed or extra, early or
late, with a way to go straight to the problem. Until this exists, the
judgement is only test output.

## What Changes

- **A take under review**, shared through `TakeRecorder`: which take, against
  which part, and its note report.
  - When a take ends, it becomes the one under review automatically, judged
    against the part it matches best (decision 2).
  - The reader can choose another take or part.
- **The Score View draws the review** on the notation:
  - correct notes ringed green;
  - wrong notes filled red, with the pitch that was played;
  - missed notes ringed grey;
  - early and late notes marked with an arrow;
  - extra notes marked at their time above the staff.

  **Show take review** in its menu turns it off and on.
- **A new Take Review tool** lists every result in score order: bar, beat,
  pitch, outcome and timing. Clicking a row moves the practice clock there, so
  the Score View and the transport follow. Its header chooses the take and
  the part, and sums the take up.
- **`TakeRecorder::keep`** adopts a take recorded elsewhere. #39 will use it to
  bring back saved takes; for now the capture harness uses it to show a review
  without a performer.

## Capabilities

### New Capabilities

- `take-review`: a take's note-by-note result on the score and in a list.

## Impact

- `src/platform/evaluation/TakeReview.h` (JUCE-free rows, best part).
- `TakeRecorder` (the review, `keep`).
- `ScoreViewComponent` (the overlay).
- `src/features/review/TakeReviewComponent`, plus one catalog entry and one
  factory.
- An approved state with a synthetic keyboard take under review, and the wiki.
- Moved to #287: uncertain sections, and long-song summaries, as #52 already
  says.
