## 1. A seam to test through

- [x] 1.1 Extract the recovery step — close, then reopen — behind something a test can replace
- [x] 1.2 Keep it on the message thread for this step; no behaviour changes yet
- [x] 1.3 Tests for the seam against the current behaviour, so the move can be compared to something

## 2. Off the message thread

- [x] 2.1 Run the recovery on its own thread; the timer asks and returns
- [x] 2.2 `recovering` becomes atomic, set before the work and cleared after, and gates the next attempt
- [x] 2.3 The thread holds the manager and nothing else, and reports only through atomics
- [x] 2.4 Test: a tick during a blocked recovery returns promptly and asks for nothing
- [x] 2.5 Test: the second attempt happens once the first finishes and the input is still unusable

## 3. Saying so

- [x] 3.1 A published input state for "opening the input device", distinct from having none
- [x] 3.2 The shell renders it without a modal and without blocking anything
- [x] 3.3 Test: the state appears while a recovery is in flight and goes when it succeeds

## 4. Shutdown

- [x] 4.1 Closing does not join a recovery that has not returned
- [x] 4.2 Nothing the recovery touches is destroyed before it — stated at the declaration, not only in the design
- [x] 4.3 Test: a service destroyed during a blocked recovery does not block
- [x] 4.4 Run that test under AddressSanitizer, which is what would catch the mistake this risks

## 5. Verification

- [x] 5.1 `ctest` green, and the `asan` and `tsan` suites green
- [x] 5.2 Held the device from a first instance and started a second: it answered in 5.8s instead
      of wedging, and photographed with "Opening mic..." in the title bar
- [x] 5.3 The control channel answered under exactly those conditions — 34 states, 5.8s — which is
      what captured nothing at all before
- [x] 5.4 Confirmed headlessly against a real capture device on this machine (Xvfb display, no
      window on the real desktop): launched, opened `tuner-docked` with no tone, polled `status`
      every 500ms for 15s. `input` on the first poll, held for the whole window, `opening` never
      seen. Clean exit, nothing left running.
- [x] 5.5 Decided: a stuck recovery is never given up on and never replaced. The one-at-a-time
      flag already gives this — a second attempt would wait on what the first is waiting on, and
      abandoning threads to keep trying leaks one per attempt on a machine that stays busy. One
      abandoned thread and one manager is the whole cost, however long the device stays held.

## 6. Found while implementing

- [x] 6.1 The manager was a by-value member, so the design's premise was false — held through a
      shared pointer the recovery thread copies
- [x] 6.2 The destructor removes the audio callback unconditionally; safe because `open()` does not
      hold JUCE's callback lock, read out of the JUCE source
- [x] 6.3 The destructor does not close the device while a recovery is in flight — closing destroys
      the object that recovery may be inside `open` on
- [x] 6.4 The warning banner said "No microphone detected" beside a title bar saying "Opening
      mic..." — the exact confusion the state was added to remove. It reads the state now.
- [x] 6.5 Seen: held the device from a first instance, started a second, photographed it
