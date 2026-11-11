# Features

Every user-facing feature Practice Takes has or plans, grouped up to three levels
deep: category, subcategory, and where a subcategory is large enough to need it,
a sub-subcategory. **Status** is the release it is planned for (see [[Roadmap]]
for the dates); ✅ means you can use it today.

Two lanes are built side by side, so neither waits for the other:
**Listening and pitch** (live tools that need no score) and **Piano practice**
(learning a piece at the keyboard). Singing practice draws on both, and the
practice loop underneath is shared.

## The categories at a glance

- **[Listening and pitch](#listening-and-pitch)**
  - Pitch: *the note you're on* · *the note you aim for*
  - Sound: *frequency content* · *level and envelope*
  - Rhythm
- **[Singing practice](#singing-practice)**
  - Your voice
  - Singing a piece: *lining up with the music* · *singing along* · *how it was sung*
- **[Piano practice](#piano-practice)**
  - Playing in
  - Learning a piece
  - How it was played: *notes and chords* · *length and the pedal* · *touch*
  - Seeing the piece
- **[Ear training and music theory](#ear-training-and-music-theory)**
  - Ear training: *singing it back* · *naming what you hear*
  - Music theory: *learning it* · *in your score*
- **[Scores and notation](#scores-and-notation)**
  - Opening scores: *formats* · *your library*
  - Reading the score
  - Changing the score
- **[The practice loop and review](#the-practice-loop-and-review)**
  - During a take: *keeping time* · *the take itself* · *guidance while you play*
  - After a take: *note by note* · *the whole take*
  - Across takes
  - Practice habits
- **[Recordings and analysis](#recordings-and-analysis)**
  - Recording
  - Analysing a recording
- **[Workspace and settings](#workspace-and-settings)**
  - Arranging tools: *placing tools* · *saved layouts*
  - Finding and controlling
  - Settings and feedback
- **[Getting started, access and reliability](#getting-started-access-and-reliability)**
  - Getting started
  - Access
  - Reliability
- **[After 1.0: making it your own](#after-10-making-it-your-own)**

## Listening and pitch

Live tools for any voice or instrument. No score needed.

### Pitch

#### The note you're on

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Tuner | The note you're on, its frequency, and how many cents sharp or flat, with a pitch graph | ✅ | — |
| Note view | The note you're on, lit up on a piano keyboard and drawn on a staff, beside your target, with a short trail | ✅ | [#297](https://github.com/dwerkjem/PracticeTakes/issues/297) |

#### The note you aim for

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Shared target note and reference pitch | One target note every tool can see, and one A4 everything measures against | ✅ | [#227](https://github.com/dwerkjem/PracticeTakes/issues/227) |
| Target note picker | Choose the note to aim at on a keyboard, on a staff, in the score, by name, or by singing it | ✅ | [#296](https://github.com/dwerkjem/PracticeTakes/issues/296) |
| Reference tone and drone | Hear the target note, or hold a drone to sing in tune against | MVP | [#299](https://github.com/dwerkjem/PracticeTakes/issues/299) |

### Sound

#### Frequency content

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Spectrogram | A scrolling view of the frequencies in the sound | ✅ | — |
| Harmonic Analyzer | The strength of each harmonic of a held note, and its brightness | ✅ | — |

#### Level and envelope

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Input level meter | The microphone's level and its peak, with the line a take ignores sound below | ✅ | [#381](https://github.com/dwerkjem/PracticeTakes/issues/381) |
| Loudness meter | Peak, RMS and LUFS loudness | Beta 1 | [#25](https://github.com/dwerkjem/PracticeTakes/issues/25) |
| Sustain and decay | How a note holds and fades | Beta 1 | [#27](https://github.com/dwerkjem/PracticeTakes/issues/27) |

### Rhythm

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Tempo and rhythm stability | The tempo you're keeping, and how steadily | Beta | [#29](https://github.com/dwerkjem/PracticeTakes/issues/29) |

## Singing practice

### Your voice

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Vocal range | Find your lowest and highest notes, keep them, and see a piece's range against yours | MVP | [#298](https://github.com/dwerkjem/PracticeTakes/issues/298) |
| Steadiness drill | Hold one note, then see how long it stayed in tune and how far it drifted | Beta 1 | [#303](https://github.com/dwerkjem/PracticeTakes/issues/303) |

### Singing a piece

#### Lining up with the music

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Latency calibration | Measures your microphone's and speakers' delay, so a sung note lines up with the music | MVP | [#38](https://github.com/dwerkjem/PracticeTakes/issues/38) |
| Drift correction | Timing stays lined up over a long take | Beta 1 | [#49](https://github.com/dwerkjem/PracticeTakes/issues/49) |

#### Singing along

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Accompaniment | The score's other parts play while yours is muted | ✅ | [#286](https://github.com/dwerkjem/PracticeTakes/issues/286) |
| Sing-along | Sing your part with the accompaniment, and get the take reviewed | MVP | [#37](https://github.com/dwerkjem/PracticeTakes/issues/37) |

#### How it was sung

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Pitch judged against the score | Each sung note marked in tune, sharp or flat, with how sure the judgement is | ✅ | [#42](https://github.com/dwerkjem/PracticeTakes/issues/42) |
| Vibrato | Vibrato recognised for what it is, with its rate and width, not marked as out of tune | Beta 1 | [#46](https://github.com/dwerkjem/PracticeTakes/issues/46) |
| Phrasing and breath | Phrases, breaths and articulation found in a take | Beta 1 | [#47](https://github.com/dwerkjem/PracticeTakes/issues/47) |

## Piano practice

### Playing in

| Feature | What it does | Status | Issue |
|---|---|---|---|
| MIDI keyboard and sustain pedal | Live capture of keys, velocity and pedal | ✅ (hardware check pending) | [#199](https://github.com/dwerkjem/PracticeTakes/issues/199) |
| Acoustic piano by microphone | Judge a real piano without a MIDI connection | Deferred: a research problem | [#204](https://github.com/dwerkjem/PracticeTakes/issues/204) |

### Learning a piece

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Hands separately, slower | Loop a passage one hand at a time, below tempo, the other hand muted or played for you | ✅ | [#201](https://github.com/dwerkjem/PracticeTakes/issues/201) |
| Wait mode | The score waits at each note or chord until you play it, with no tempo and no penalty | Beta 1 | [#304](https://github.com/dwerkjem/PracticeTakes/issues/304) |
| Fingering | The score's own fingering numbers, shown or hidden | Beta 1 | [#305](https://github.com/dwerkjem/PracticeTakes/issues/305) |

### How it was played

#### Notes and chords

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Chords | Whether every note of a chord sounded, together and evenly | Beta 1 | [#202](https://github.com/dwerkjem/PracticeTakes/issues/202) |

#### Length and the pedal

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Held notes and the pedal | Whether a note was held its full length, and a pedalled note not marked short | Beta 1 | [#44](https://github.com/dwerkjem/PracticeTakes/issues/44), [#203](https://github.com/dwerkjem/PracticeTakes/issues/203) |

#### Touch

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Dynamics and evenness | Loud and soft, crescendos, and how even a run is (judged for the voice too) | Beta 1 | [#45](https://github.com/dwerkjem/PracticeTakes/issues/45) |

### Seeing the piece

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Piano roll | A MIDI piece shown as a piano roll as well as notation | Beta 2 | [#35](https://github.com/dwerkjem/PracticeTakes/issues/35) |

## Ear training and music theory

Training what you hear and understand, alongside what you play and sing.

### Ear training

#### Singing it back

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Pitch matching | Hear a note, then sing it back; see how close you landed and how fast | Beta 1 | [#301](https://github.com/dwerkjem/PracticeTakes/issues/301) |
| Interval drills | Sing an interval above or below a note, and hear what you sang beside what was asked | Beta 1 | [#302](https://github.com/dwerkjem/PracticeTakes/issues/302) |

#### Naming what you hear

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Ear training by recognition | Name intervals, chords, scales and modes by ear; melodic and rhythmic dictation; chord progressions | After 1.0 | [#309](https://github.com/dwerkjem/PracticeTakes/issues/309) |

### Music theory

#### Learning it

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Music theory lessons | From the staff and key signatures to chords and harmony, with exercises answered on a keyboard, a staff, or by singing | After 1.0 | [#310](https://github.com/dwerkjem/PracticeTakes/issues/310) |

#### In your score

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Theory in your score | The key, chords, Roman numerals and intervals shown over the piece you're practising | After 1.0 | [#311](https://github.com/dwerkjem/PracticeTakes/issues/311) |

## Scores and notation

### Opening scores

#### Formats

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Open MusicXML | `.musicxml`, `.xml` and `.mxl`, with a summary of anything that came in differently | ✅ | — |
| MIDI files | Open a MIDI file to display, play as accompaniment, or practise against | Beta 2 | [#34](https://github.com/dwerkjem/PracticeTakes/issues/34) |

#### Your library

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Library | Songs and takes in one folder, listed under File → Library…: every score you open is copied in, and every take is saved | ✅ | — |
| Score library | The folders you choose, listed with a preview of each score | Beta 2 | [#228](https://github.com/dwerkjem/PracticeTakes/issues/228) |

### Reading the score

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Score View | Engraved notation with page, vertical and horizontal views, sizing, and showing or hiding parts | ✅ | — |
| Score cursor | The practice clock's place in the score, the notes sounding and the next few marked, followed page by page as it plays; click a note or go to a bar to start from there; fit a whole page | ✅ | [#33](https://github.com/dwerkjem/PracticeTakes/issues/33) |
| Rehearsal marks | Keep a score's rehearsal marks, and jump to one | Not yet scheduled | [#324](https://github.com/dwerkjem/PracticeTakes/issues/324) |
| One voice of a part | Choose which voice of a shared staff is yours, such as the alto in an SATB score | Not yet scheduled | [#325](https://github.com/dwerkjem/PracticeTakes/issues/325) |

### Changing the score

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Fix a wrong note | Correct a mistake in the score without leaving the app | After 1.0 | [#229](https://github.com/dwerkjem/PracticeTakes/issues/229) |

## The practice loop and review

Shared by every instrument.

### During a take

#### Keeping time

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Practice clock | Play, pause, stop and a count-in; a metronome on every beat, the first of each bar accented; the tempo from 5% to 200%; and looping the score | ✅ | [#36](https://github.com/dwerkjem/PracticeTakes/issues/36) |

#### The take itself

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Takes | Record a take from the microphone, a MIDI keyboard or both, counted in, against the score; stop it part-way or play to the end | ✅ | [#40](https://github.com/dwerkjem/PracticeTakes/issues/40) |

#### Guidance while you play

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Current and next note | The note to play or sing now, and the ones coming | ✅ | [#41](https://github.com/dwerkjem/PracticeTakes/issues/41) |
| Live feedback | A restrained signal during the take, not a distraction | Beta | [#51](https://github.com/dwerkjem/PracticeTakes/issues/51) |

### After a take

#### Note by note

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Wrong, missed and extra notes | Every note of a take matched to the score | ✅ | [#48](https://github.com/dwerkjem/PracticeTakes/issues/48) |
| Timing | Each note early, late or on time | ✅ | [#43](https://github.com/dwerkjem/PracticeTakes/issues/43) |
| Note-by-note review | Every note marked on the score after the take; click one to go there | ✅ | [#52](https://github.com/dwerkjem/PracticeTakes/issues/52) |

#### The whole take

| Feature | What it does | Status | Issue |
|---|---|---|---|
| An overall result, explained | One summary of the take that shows what it is made of | Beta | [#50](https://github.com/dwerkjem/PracticeTakes/issues/50) |
| Phrase review and suggestions | What went wrong by phrase, and what to practise next | Beta | [#287](https://github.com/dwerkjem/PracticeTakes/issues/287) |

### Across takes

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Tempo ladder | The tempo rises a step after each clean take, up to your goal | Beta | [#306](https://github.com/dwerkjem/PracticeTakes/issues/306) |
| Compare takes | Keep takes and compare them | Beta | [#53](https://github.com/dwerkjem/PracticeTakes/issues/53) |
| Retry weak spots | Go straight to the passages that went wrong, and export a report | Beta | [#54](https://github.com/dwerkjem/PracticeTakes/issues/54) |
| Sessions | Save a score, its takes, layout and practice settings together, and reopen them | Beta 2 | [#39](https://github.com/dwerkjem/PracticeTakes/issues/39) |

### Practice habits

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Practice log | How long you practised each day, and on which pieces and drills, kept for you | Beta | [#307](https://github.com/dwerkjem/PracticeTakes/issues/307) |

## Recordings and analysis

Anything you can look at live, you can look at on a recording
([#291](https://github.com/dwerkjem/PracticeTakes/issues/291)).

### Recording

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Saved takes | Every take's audio and keyboard kept in the library with its score, and reviewed again in a later session | ✅ | — |
| Record and open recordings | Keep a take's audio and MIDI, or open a WAV, AIFF or FLAC file | Beta 2 | [#292](https://github.com/dwerkjem/PracticeTakes/issues/292) |

### Analysing a recording

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Every tool on a recording | The same tools as live, with play, pause, seek and scrub | Beta | [#293](https://github.com/dwerkjem/PracticeTakes/issues/293) |
| Whole-take graphs | Pitch, loudness, spectrum and harmonics over a whole take, with the score's targets overlaid | Beta | [#294](https://github.com/dwerkjem/PracticeTakes/issues/294) |

## Workspace and settings

### Arranging tools

#### Placing tools

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Dock, float, tile and tab tools | Arrange tools in the window or in their own windows, without restarting them | ✅ | — |
| Two of the same tool | Open a second tuner or spectrogram side by side, and name each copy | ✅ | [#135](https://github.com/dwerkjem/PracticeTakes/issues/135) |
| Docked tools use their room | A docked tool shows more when given more space | 1.0 | [#151](https://github.com/dwerkjem/PracticeTakes/issues/151) |

#### Saved layouts

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Workspaces | Four built-in layouts, plus your own named ones | ✅ | — |

### Finding and controlling

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Mute you can see | Being muted obvious at a glance | ✅ | [#114](https://github.com/dwerkjem/PracticeTakes/issues/114) |
| Tool categories and favourites | Find tools faster as there are more of them | 1.0 | [#134](https://github.com/dwerkjem/PracticeTakes/issues/134) |
| Keyboard shortcuts | Shortcuts for the shell and tools, and an editor for them | 1.0 | [#140](https://github.com/dwerkjem/PracticeTakes/issues/140), [#141](https://github.com/dwerkjem/PracticeTakes/issues/141) |

### Settings and feedback

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Portable settings | Export and import your settings as a `.ptsettings` file | ✅ (layouts and presets to come) | [#59](https://github.com/dwerkjem/PracticeTakes/issues/59) |
| Feedback form | Send feedback from any tool or from Help | ✅ | — |
| Search | One search bar (the magnifying glass, or Ctrl+K) over settings, tools, commands, workspaces, your songs and takes, and the open score's lyrics and bars; choosing a result goes there | ✅ | — |

## Getting started, access and reliability

### Getting started

| Feature | What it does | Status | Issue |
|---|---|---|---|
| First-run guide | Choose your input and start a guided practice session | 1.0 | [#57](https://github.com/dwerkjem/PracticeTakes/issues/57) |
| User manual | A full manual beyond this wiki | 1.0 | [#139](https://github.com/dwerkjem/PracticeTakes/issues/139) |

### Access

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Accessibility | Usable from the keyboard, a MIDI controller and a screen reader | 1.0 | [#58](https://github.com/dwerkjem/PracticeTakes/issues/58) |
| Translations | Practice Takes in more than one language | 1.0 | [#285](https://github.com/dwerkjem/PracticeTakes/issues/285) |

### Reliability

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Crash recovery | Autosave, and nothing lost after an interrupted session | 1.0 | [#55](https://github.com/dwerkjem/PracticeTakes/issues/55) |

## After 1.0: making it your own

| Feature | What it does | Status | Issue |
|---|---|---|---|
| Your own practice content | Exercises and routines as files you can write and share | After 1.0 | [#188](https://github.com/dwerkjem/PracticeTakes/issues/188) |
| Scripting | Drive the tools from your own code | After 1.0 | [#136](https://github.com/dwerkjem/PracticeTakes/issues/136) |

## Ideas not on the roadmap yet

None right now: every idea suggested so far has an issue and a place above.
New ones are welcome through **Give feedback on this tool**, or
[the issue tracker](https://github.com/dwerkjem/PracticeTakes/issues/new/choose).
Adding one to the roadmap moves a date or replaces something of the same size,
so each needs a decision first.
