# Roadmap

The authoritative roadmap, with its evidence and a **Live forecast** that updates
every time an issue is closed, is
[issue #10](https://github.com/dwerkjem/PracticeTakes/issues/10). This page is
the summary.

## The order

**Foundation → MVP → Beta → 1.0**, then extensibility after 1.0.

| Stage | What you get | Commitment | Planning target |
|---|---|---|---|
| **Foundation** ([#12](https://github.com/dwerkjem/PracticeTakes/issues/12)) | The dockable workspace and its tools: what exists today. A development build, not a product | 29 Nov 2026 | 16 Nov 2026 |
| **MVP** ([#13](https://github.com/dwerkjem/PracticeTakes/issues/13)) | **Piano and voice together.** Play a piece on a MIDI keyboard, or sing it with the score as accompaniment. See on the score what went wrong in the take, and loop the weak passage slower | **12 Apr 2027** | 8 Feb 2027 |
| Beta 1 ([#14](https://github.com/dwerkjem/PracticeTakes/issues/14)) | Depth on both instruments: held notes, the pedal and chords on the piano; vibrato, phrasing, breath and latency drift for the voice; dynamics for both | 26 Jul 2027 | 19 Apr 2027 |
| Beta 2 | Files and sessions: MIDI files, saved sessions, the piano roll, a score library, and recording takes and opening audio files | 27 Sep 2027 | 31 May 2027 |
| **Beta** | Review and history: an explained overall result, phrase-level review, saved and compared takes, retrying weak sections. **Every tool works on a recording as it does live**, with whole-take graphs ([#291](https://github.com/dwerkjem/PracticeTakes/issues/291)) | **7 Feb 2028** | 16 Aug 2027 |
| **1.0** ([#15](https://github.com/dwerkjem/PracticeTakes/issues/15)) | Crash recovery, onboarding, accessibility, translations, verified on Windows, Linux and macOS | **28 Aug 2028** | 29 Nov 2027 |
| After 1.0 ([#188](https://github.com/dwerkjem/PracticeTakes/issues/188)) | Authorable practice content, scripting, live score editing | not committed | — |

## Two kinds of date

- A **commitment** has an 80% chance of being met if the project keeps working
  the way it has. It is the date the project is held to.
- A **planning target** is the 50% point: the date the work is planned against.

Both come from a forecast built on this repository's own history. That history
includes how much work an active week closes, how often weeks go idle, and how
fast scope has grown. **The biggest risk is a long idle stretch**, like the one
between late August and mid-October 2026.

## How it is kept honest

- Every issue between now and 1.0 has a size, in a ledger kept in the repository.
- Whenever an issue is closed or reopened, the forecast is re-run and #10's Live
  forecast shows each commitment as 🟢 on track, 🟠 at risk or 🔴 behind.
- A monthly checkpoint says how much work should have been delivered by then. If
  delivery falls below it, scope is cut from the next release before any date
  moves.
- A commitment only moves by a decision recorded in #10, with its reason.

How the forecast works is described in
[`ROADMAP_FORECAST.md`](https://github.com/dwerkjem/PracticeTakes/blob/main/docs/development/operations/ROADMAP_FORECAST.md).
