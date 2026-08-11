## Why

A run cannot be stopped, and there is nothing in the interface that says so.

A default full sweep is 204 captures. Once it starts there is no button, no key,
and no documented command that ends it. The person who started it either waits,
or works out for themselves that the pass restarts the application between
surfaces so killing the window achieves nothing, and goes looking for the right
process to signal.

That happened today. The run was ended by finding the process by hand, and it
took three attempts because the application it kept restarting was not the thing
holding the run.

Reviewing has the mirror problem at a smaller scale. A run's captures can be
narrowed to a subset and approved together, but a comment goes on one capture at
a time -- so the same sentence about the same defect gets typed once per image,
which is what happened to the four captures that all said "the lettering should
have no border".

## What Changes

- **A run can be stopped while it is running**, from the interface that started
  it, and from the keyboard. What was captured before the stop is kept; what was
  not is left uncaptured rather than recorded as failed, because "we stopped"
  and "it did not work" are different things and only one of them is a defect.
- **A comment can be written once against several captures**, alongside the
  narrowing the review already offers.

## Non-goals

- **No pause and resume.** A stopped run is resumed by running it again;
  already-captured surfaces are skipped, which is the mechanism that exists.
- **No cancelling an individual capture.** The unit is the run.
- **No change to what a capture or a verdict is.**

## Capabilities

### Modified Capabilities

- `ui-capture-and-review`: stopping a run in progress, and commenting on more
  than one capture at once.

## Impact

- `tools/scripts/testing_suite/capture.py` — the pass checks whether it has been
  asked to stop.
- `tools/scripts/testing_suite/runner.py`, `server.py`, `web/` — the button, the
  key, and the endpoint.
- `tools/scripts/testing_suite/review.py`, `store.py` — one comment, several
  captures.
- **Not affected:** the store schema for captures, the export, and the release
  gate.
