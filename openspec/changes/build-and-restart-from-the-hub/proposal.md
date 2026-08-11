## Why

Two ways the hub makes you leave it to do the thing it is for.

**Building.** The build is the slowest part of everything the hub runs, and it
is what a source change invalidates. But there is no way to ask for one. A build
happens only on the way to a suite, so wanting a current binary — to launch the
application by hand, to check a change compiles, to stop a capture pass
photographing last week's build — means starting a run you did not want and
stopping it once the build is done, or leaving for a terminal.

The build state is reported, but only as prose. Two targets, each either
missing, current, or older than the source, described in a paragraph under the
heading. Whether there is anything to run is the first question anyone has on
opening the hub, and it is answered in a sentence that has to be read.

**Restarting.** The hub imports its modules once and serves its page from disk
on every request, so editing the suite while it is open leaves a current page
talking to hour-old code. That cost a session today: deleting a comment did
nothing and comments rendered blank, because the page was posting to a route the
running process had never heard of and reading a field it had never sent. Both
halves were correct.

Nothing in the hub said so, and nothing in it offered the fix. The answer was a
terminal behind the browser, a Ctrl-C, and a command — which is a lot to work
out from a button that silently does nothing.

## What Changes

- **A build can be asked for on its own**, per target, without a run behind it.
  A build verified nothing, so it records nothing: a run row with no captures
  and no results under it would sit in the history looking like a run that did.
- **The build state is a colour before it is a sentence** — one button per
  target, red when it is not built and green when it is, doing the build it
  describes when pressed.
- **The notices about building are actionable.** "Tick rebuild and run
  something" is replaced by a button that rebuilds that target, in the sentence
  that raised it.
- **The hub says when it is running code older than the source on disk**, on
  every view rather than only where a run is started.
- **That warning carries the way out.** A restart button, only there, replacing
  the process in place so the page it is pressed from comes back to itself.

## Non-goals

- Watching the source and restarting on its own. A hub that restarts underneath
  a review, or during a capture pass, would be a worse failure than the one this
  fixes.
- Rebuilding automatically when a target goes stale. Several minutes is not
  something to start without being asked.
- Any change to what a run is, what gets captured, or how a verdict is recorded.
