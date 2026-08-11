## 1. Stopping a run

- [x] 1.1 `CapturePass.run` takes something it can ask whether to stop, checked at each surface boundary (design decision 1)
- [x] 1.2 Surfaces not reached are left unattempted, not recorded as failures (design decision 2)
- [x] 1.3 The application and the virtual display are shut down on the way out, as they are on a normal finish
- [x] 1.4 Tests over the loop: a run stopped part way captures the surfaces before the stop and none after, and reports no failures

## 2. Reaching it from the hub

- [x] 2.1 A stop control on the run view, shown only while a run is in progress
- [x] 2.2 An endpoint behind it, refusing politely when no run is in progress
- [x] 2.3 Escape does the same thing (design decision 3)
- [x] 2.4 The view says the run was stopped rather than that it finished

## 3. Continuing afterwards

- [x] 3.1 Confirm running again skips what the stopped run captured — the existing resume, exercised rather than assumed

## 4. Commenting in bulk

- [x] 4.1 One comment written against every capture in the current selection, using the narrowing the review already has
- [x] 4.2 One row per capture, so a capture read alone still shows it (design decision 4)
- [x] 4.3 Tests: a comment over a selection reaches exactly those captures and no others

## 4b. Removing a comment

- [x] 4b.1 Delete one comment from a capture, leaving its others alone
- [x] 4b.2 Comments carry their id to the page, since position in a list is not a name
- [x] 4b.3 Deleting one that is not there says so rather than reporting success
- [x] 4b.4 The control is quiet until its comment is hovered -- a row of crosses invites misclicks on the thing they remove

## 4c. Bulk actions appear only when they apply

- [x] 4c.1 Pass, fail, skip, and comment for a selection are hidden until more than one capture is selected

## 5. Verification

- [x] 5.1 `python tools/scripts/run_tests.py` green
- [x] 5.2 Stop a real sweep part way; confirm the captures before it are reviewable and nothing is marked failed
- [x] 5.3 Run it again and confirm it resumes rather than starting over
- [x] 5.4 Comment on several captures at once and confirm each carries it
- [x] 5.5 Delete a comment through the running hub, and press delete twice on the same one
