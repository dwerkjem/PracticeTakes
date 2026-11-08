# Features

Every user-facing feature Practice Takes has or plans, grouped by what you'd use
it for. **Status** is the release it is planned for (see [[Roadmap]] for the
dates); ✅ means you can use it today.

Two lanes are built side by side, so neither waits for the other:
**Listening and pitch** (live tools that need no score) and **Piano practice**
(learning a piece at the keyboard). Singing practice draws on both, and the
practice loop underneath is shared.

## Listening and pitch

Live tools for any voice or instrument. No score needed.

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Tuner | The note you're on, its frequency, and how many cents sharp or flat, with a pitch graph | ✅ | — |
| Spectrogram | A scrolling view of the frequencies in the sound | ✅ | — |
| Harmonic Analyzer | The strength of each harmonic of a held note, and its brightness | ✅ | — |
| Shared target note and reference pitch | One target note every tool can see, and one A4 everything measures against | MVP | [#227](https://github.com/dwerkjem/PracticeTakes/issues/227) |
| Target note picker | Choose the note to aim at on a keyboard, on a staff, in the score, by name, or by singing it | MVP | [#296](https://github.com/dwerkjem/PracticeTakes/issues/296) |
| Note view | The note you're on, lit up on a piano keyboard and drawn on a staff, beside your target, with a short trail | MVP | [#297](https://github.com/dwerkjem/PracticeTakes/issues/297) |
| Vocal range | Find your lowest and highest notes, keep them, and see a piece's range against yours | MVP | [#298](https://github.com/dwerkjem/PracticeTakes/issues/298) |
| Reference tone and drone | Hear the target note, or hold a drone to sing in tune against | MVP | [#299](https://github.com/dwerkjem/PracticeTakes/issues/299) |
| Loudness meter | Peak, RMS and LUFS loudness | Beta 1 | [#25](https://github.com/dwerkjem/PracticeTakes/issues/25) |
| Sustain and decay | How a note holds and fades | Beta 1 | [#27](https://github.com/dwerkjem/PracticeTakes/issues/27) |
| Tempo and rhythm stability | The tempo you're keeping, and how steadily | Beta | [#29](https://github.com/dwerkjem/PracticeTakes/issues/29) |

## Singing practice

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Latency calibration | Measures your microphone's and speakers' delay, so a sung note lines up with the music | MVP | [#38](https://github.com/dwerkjem/PracticeTakes/issues/38) |
| Pitch judged against the score | Each sung note marked in tune, sharp or flat, with how sure the judgement is | MVP | [#42](https://github.com/dwerkjem/PracticeTakes/issues/42) |
| Accompaniment | The score's other parts play while yours is muted | MVP | [#286](https://github.com/dwerkjem/PracticeTakes/issues/286) |
| Sing-along | Sing your part with the accompaniment, and get the take reviewed | MVP | [#37](https://github.com/dwerkjem/PracticeTakes/issues/37) |
| Vibrato | Vibrato recognised for what it is, with its rate and width, not marked as out of tune | Beta 1 | [#46](https://github.com/dwerkjem/PracticeTakes/issues/46) |
| Phrasing and breath | Phrases, breaths and articulation found in a take | Beta 1 | [#47](https://github.com/dwerkjem/PracticeTakes/issues/47) |
| Drift correction | Timing stays lined up over a long take | Beta 1 | [#49](https://github.com/dwerkjem/PracticeTakes/issues/49) |

## Piano practice

| Feature | What it does | Status | Issue |
|---|---|---|---|
| MIDI keyboard and sustain pedal | Live capture of keys, velocity and pedal | ✅ (hardware check pending) | [#199](https://github.com/dwerkjem/PracticeTakes/issues/199) |
| Hands separately, slower | Loop a passage one hand at a time, below tempo, the other hand muted or played for you | MVP | [#201](https://github.com/dwerkjem/PracticeTakes/issues/201) |
| Held notes and the pedal | Whether a note was held its full length, and a pedalled note not marked short | Beta 1 | [#44](https://github.com/dwerkjem/PracticeTakes/issues/44), [#203](https://github.com/dwerkjem/PracticeTakes/issues/203) |
| Chords | Whether every note of a chord sounded, together and evenly | Beta 1 | [#202](https://github.com/dwerkjem/PracticeTakes/issues/202) |
| Dynamics and evenness | Loud and soft, crescendos, and how even a run is | Beta 1 | [#45](https://github.com/dwerkjem/PracticeTakes/issues/45) |
| Piano roll | A MIDI piece shown as a piano roll as well as notation | Beta 2 | [#35](https://github.com/dwerkjem/PracticeTakes/issues/35) |

## Scores and notation

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Open MusicXML | `.musicxml`, `.xml` and `.mxl`, with a summary of anything that came in differently | ✅ | — |
| Score View | Engraved notation with page, vertical and horizontal views, sizing, and showing or hiding parts | ✅ | — |
| Score cursor | The current note followed through the score as you play | MVP | [#33](https://github.com/dwerkjem/PracticeTakes/issues/33) |
| MIDI files | Open a MIDI file to display, play as accompaniment, or practise against | Beta 2 | [#34](https://github.com/dwerkjem/PracticeTakes/issues/34) |
| Score library | The folders you choose, listed with a preview of each score | Beta 2 | [#228](https://github.com/dwerkjem/PracticeTakes/issues/228) |
| Fix a wrong note | Correct a mistake in the score without leaving the app | After 1.0 | [#229](https://github.com/dwerkjem/PracticeTakes/issues/229) |

## The practice loop and review

Shared by every instrument.

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Practice clock | Count-in, metronome, tempo, and looping a passage | MVP | [#36](https://github.com/dwerkjem/PracticeTakes/issues/36) |
| Takes | Start, count in, play or sing, stop | MVP | [#40](https://github.com/dwerkjem/PracticeTakes/issues/40) |
| Timing | Each note early, late or on time | MVP | [#43](https://github.com/dwerkjem/PracticeTakes/issues/43) |
| Wrong, missed and extra notes | Every note of a take matched to the score | MVP | [#48](https://github.com/dwerkjem/PracticeTakes/issues/48) |
| Note-by-note review | Every note marked on the score after the take; click one to go there | MVP | [#52](https://github.com/dwerkjem/PracticeTakes/issues/52) |
| Current and next note | The note to play or sing now, and the ones coming, during a take | MVP | [#41](https://github.com/dwerkjem/PracticeTakes/issues/41) |
| Live feedback | A restrained signal during the take, not a distraction | Beta | [#51](https://github.com/dwerkjem/PracticeTakes/issues/51) |
| An overall result, explained | One summary of the take that shows what it is made of | Beta | [#50](https://github.com/dwerkjem/PracticeTakes/issues/50) |
| Phrase review and suggestions | What went wrong by phrase, and what to practise next | Beta | [#287](https://github.com/dwerkjem/PracticeTakes/issues/287) |
| Compare takes | Keep takes and compare them | Beta | [#53](https://github.com/dwerkjem/PracticeTakes/issues/53) |
| Retry weak spots | Go straight to the passages that went wrong, and export a report | Beta | [#54](https://github.com/dwerkjem/PracticeTakes/issues/54) |
| Sessions | Save a score, its takes, layout and practice settings together, and reopen them | Beta 2 | [#39](https://github.com/dwerkjem/PracticeTakes/issues/39) |

## Recordings and analysis

Anything you can look at live, you can look at on a recording
([#291](https://github.com/dwerkjem/PracticeTakes/issues/291)).

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Record and open recordings | Keep a take's audio and MIDI, or open a WAV, AIFF or FLAC file | Beta 2 | [#292](https://github.com/dwerkjem/PracticeTakes/issues/292) |
| Every tool on a recording | The same tools as live, with play, pause, seek and scrub | Beta | [#293](https://github.com/dwerkjem/PracticeTakes/issues/293) |
| Whole-take graphs | Pitch, loudness, spectrum and harmonics over a whole take, with the score's targets overlaid | Beta | [#294](https://github.com/dwerkjem/PracticeTakes/issues/294) |

## Workspace and settings

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Dock, float, tile and tab tools | Arrange tools in the window or in their own windows, without restarting them | ✅ | — |
| Workspaces | Four built-in layouts, plus your own named ones | ✅ | — |
| Portable settings | Export and import your settings as a `.ptsettings` file | ✅ (layouts and presets to come) | [#59](https://github.com/dwerkjem/PracticeTakes/issues/59) |
| Feedback form | Send feedback from any tool or from Help | ✅ | — |
| Mute you can see | Being muted obvious at a glance | Foundation | [#114](https://github.com/dwerkjem/PracticeTakes/issues/114) |
| Two of the same tool | Open a second tuner or spectrogram side by side | Foundation | [#135](https://github.com/dwerkjem/PracticeTakes/issues/135) |
| Keyboard shortcuts | Shortcuts for the shell and tools, and an editor for them | 1.0 | [#140](https://github.com/dwerkjem/PracticeTakes/issues/140), [#141](https://github.com/dwerkjem/PracticeTakes/issues/141) |
| Tool categories and favourites | Find tools faster as there are more of them | 1.0 | [#134](https://github.com/dwerkjem/PracticeTakes/issues/134) |
| Docked tools use their room | A docked tool shows more when given more space | 1.0 | [#151](https://github.com/dwerkjem/PracticeTakes/issues/151) |

## Getting started, access and reliability

| Feature | What it does | Status | Issue |
|---|---|---|---|
| First-run guide | Choose your input and start a guided practice session | 1.0 | [#57](https://github.com/dwerkjem/PracticeTakes/issues/57) |
| Accessibility | Usable from the keyboard, a MIDI controller and a screen reader | 1.0 | [#58](https://github.com/dwerkjem/PracticeTakes/issues/58) |
| Translations | Practice Takes in more than one language | 1.0 | [#285](https://github.com/dwerkjem/PracticeTakes/issues/285) |
| Crash recovery | Autosave, and nothing lost after an interrupted session | 1.0 | [#55](https://github.com/dwerkjem/PracticeTakes/issues/55) |
| User manual | A full manual beyond this wiki | 1.0 | [#139](https://github.com/dwerkjem/PracticeTakes/issues/139) |

## After 1.0: making it your own

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Your own practice content | Exercises and routines as files you can write and share | After 1.0 | [#188](https://github.com/dwerkjem/PracticeTakes/issues/188) |
| Scripting | Drive the tools from your own code | After 1.0 | [#136](https://github.com/dwerkjem/PracticeTakes/issues/136) |
| Acoustic piano by microphone | Judge a real piano without a MIDI connection | Deferred: a research problem | [#204](https://github.com/dwerkjem/PracticeTakes/issues/204) |

## Ideas not on the roadmap yet

Suggestions that fit the two lanes but aren't sized or scheduled. Adding one to
the roadmap moves a date or replaces something of the same size, so each needs a
decision first.

- **Listening and pitch:** pitch-matching ear training (hear a note, then sing
  it); interval drills; sustain and steadiness drills on one note.
- **Piano practice:** wait mode, where the score waits until you play the right
  notes; a tempo ladder that raises the tempo after each clean take; fingering
  shown from the score.
- **Practice habits:** a practice log, with how long and what you worked on.
