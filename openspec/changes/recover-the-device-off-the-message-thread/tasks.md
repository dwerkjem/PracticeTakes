## 1. A seam to test through

- [ ] 1.1 Extract the recovery step — close, then reopen — behind something a test can replace
- [ ] 1.2 Keep it on the message thread for this step; no behaviour changes yet
- [ ] 1.3 Tests for the seam against the current behaviour, so the move can be compared to something

## 2. Off the message thread

- [ ] 2.1 Run the recovery on its own thread; the timer asks and returns
- [ ] 2.2 `recovering` becomes atomic, set before the work and cleared after, and gates the next attempt
- [ ] 2.3 The thread holds the manager and nothing else, and reports only through atomics
- [ ] 2.4 Test: a tick during a blocked recovery returns promptly and asks for nothing
- [ ] 2.5 Test: the second attempt happens once the first finishes and the input is still unusable

## 3. Saying so

- [ ] 3.1 A published input state for "opening the input device", distinct from having none
- [ ] 3.2 The shell renders it without a modal and without blocking anything
- [ ] 3.3 Test: the state appears while a recovery is in flight and goes when it succeeds

## 4. Shutdown

- [ ] 4.1 Closing does not join a recovery that has not returned
- [ ] 4.2 Nothing the recovery touches is destroyed before it — stated at the declaration, not only in the design
- [ ] 4.3 Test: a service destroyed during a blocked recovery does not block
- [ ] 4.4 Run that test under AddressSanitizer, which is what would catch the mistake this risks

## 5. Verification

- [ ] 5.1 `ctest` green, and the `asan` and `tsan` suites green
- [ ] 5.2 Hold the input device from another process, start the application, and confirm the window
      repaints, opens menus, and closes — the case that is a freeze today
- [ ] 5.3 Confirm the control channel keeps answering under the same conditions, by capturing while
      another instance is open — which captures nothing at all today
- [ ] 5.4 Confirm an ordinary machine with a working microphone is unchanged: input works, and no
      recovery state appears
- [ ] 5.5 Decide the open question about giving up, and record the answer
