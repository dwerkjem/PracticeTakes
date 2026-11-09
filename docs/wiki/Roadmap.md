# Roadmap

The authoritative roadmap, with its evidence and a **Live forecast** that updates
every time an issue is closed, is
[issue #10](https://github.com/dwerkjem/PracticeTakes/issues/10). This page is
the summary.

## The order

**Foundation → MVP → Beta → 1.0**, then extensibility, ear training and music theory after 1.0.

| Stage | What you get | Commitment | Planning target |
|---|---|---|---|
| **Foundation** ([#12](https://github.com/dwerkjem/PracticeTakes/issues/12)) | The dockable workspace and its tools: what exists today. **Released as 0.7.0**, a development build, not a product | 29 Nov 2026 | 16 Nov 2026 |
| **MVP** ([#13](https://github.com/dwerkjem/PracticeTakes/issues/13)) | **Two lanes at once.** Listening and pitch: choose a target note, see the note you're on against it, find your range, sing against a drone. Piano practice: hands separately, slower. Both lead into one practice loop: play or sing a piece, see on the score what went wrong, loop the weak passage | **17 May 2027** | 1 Mar 2027 |
| Beta 1 ([#14](https://github.com/dwerkjem/PracticeTakes/issues/14)) | Depth and drills on both instruments: held notes, the pedal, chords, wait mode and fingering on the piano; vibrato, phrasing, breath and latency drift for the voice; dynamics for both; pitch-matching, interval and steadiness drills | 4 Oct 2027 | 7 Jun 2027 |
| Beta 2 | Files and sessions: MIDI files, saved sessions, the piano roll, a score library, and recording takes and opening audio files | 13 Dec 2027 | 19 Jul 2027 |
| **Beta** | Review and history: an explained overall result, phrase-level review, saved and compared takes, retrying weak sections, a tempo ladder and a practice log. **Every tool works on a recording as it does live**, with whole-take graphs ([#291](https://github.com/dwerkjem/PracticeTakes/issues/291)) | **8 May 2028** | 11 Oct 2027 |
| **1.0** ([#15](https://github.com/dwerkjem/PracticeTakes/issues/15)) | Crash recovery, onboarding, accessibility, translations, verified on Windows, Linux and macOS | **27 Nov 2028** | 31 Jan 2028 |
| After 1.0 ([#188](https://github.com/dwerkjem/PracticeTakes/issues/188)) | Authorable practice content, scripting, live score editing | not committed | — |
| After 1.0 ([#308](https://github.com/dwerkjem/PracticeTakes/issues/308)) | Ear training by recognition (intervals, chords, scales, dictation) and music theory, connected to the score you're practising | not committed | — |

Every feature, by category and status, is on the [[Features]] page.

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
