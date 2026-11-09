## Decisions

### 1. The timeline, JUCE-free

`score::timelineOf(score, part)` returns `TimelineItem`s in time order, from the
part's first voice in each measure (the voice MusicXML numbers lowest):
- **A note:**
  - the event's sounding pitches, so a chord is one item;
  - its start and end ticks. A note tied forward runs to the end of its last
    tie, and its continuations make no item: they are not attacks.
  - `tiedOver`, set when that happened.
- **A rest:** start and end. Rests in a row merge into one.
- **The lyric:** verse 1's syllable, with a hyphen after it when the word goes
  on.
- **`repeated`:** a note with the same pitches as the note before it, even
  across a rest. A singer re-attacks it.
- **`phraseStart`:** the first note, and every note after a rest. The score
  model carries no breath marks, fermatas or slurs, so a rest is the only
  phrase boundary it can see. A later importer change can add more without
  changing the display.
- Grace and cue notes make no item, as elsewhere.

`itemAt(timeline, tick)` finds the item sounding at a tick by binary search.

### 2. When "now" is

The tool reads the practice clock 60 times a second.
- **While playing:** the frame is `timing().frameAt(now)` less the device's
  output latency, turned into a tick through the clock's table. So the current
  note changes when the singer hears the score reach it, and the remaining
  time runs down smoothly between the clock's own updates.
- **Stopped or paused:** the clock's tick.
- **Counting in:** the tick is negative. The first item is shown as next, with
  the beats until it starts.

The output latency is the device's stated figure until #38 calibrates it.

### 3. The display

- **Now:**
  - the note's name, at the largest size the card allows;
  - its lyric;
  - a five-line staff with the note, in the clef the part's range suggests;
  - its length in beats of the current meter;
  - a bar along the bottom that empties as the note runs out, with the
    seconds left.

  A rest shows "Rest", and a note held over a tie says "tied".
- **Next:** the following items as cards, 4 by default (2, 4, 6 or 8 from the
  menu). Each card shows the name, lyric and beats.
  - A rest card is dim.
  - A repeated note has an "again" badge.
  - A phrase start has a breath mark (’) and a wider gap before it.
  - The strip changes only when "now" moves on. Nothing slides, so the next
    notes stay still to read.
- **High contrast:** black, white and yellow, with heavier type. It is the
  menu's **High contrast**, saved with the tool.
- **Compact:** below 320 × 300 px only the current name, its time bar and the
  next name show.

### 4. Which part

By default it follows the part `selectTargetNote` picks: the first part that
plays one note at a time. The menu's **Follow part** lists every part.
- The choice is saved by part id and index.
- An index the next score lacks falls back to the default.

### 5. The shared target follows the score

On every clock update, the shell checks two things:
- the `practice.targetFollowsScore` setting is on (it is by default);
- the target came from the score (`TargetOrigin::score`).

When both hold, it publishes the note at the audible tick in the target's part
(`pitch::targetAtTick`). It publishes only when that note changes, and leaves
the target as it is on a rest. A target the player chose is never replaced.
When the clock stops, the target stays on the last note, and opening a score
still resets it as before.

### 6. Testing and pictures

- Unit tests cover:
  - the timeline: ties, rests merged, repeats, phrase starts, lyrics, chords,
    grace and cue notes;
  - `itemAt`;
  - the "now" tick, including latency and the count-in;
  - the look-ahead selection;
  - `targetAtTick`.
- States:
  - `now-next`: the sample, paused in bar 2;
  - `now-next-contrast`: the same in high contrast;
  - `now-next-start`: the sample at bar 1, with a rest and a breath mark in the
    strip. A "no score" picture can't be pinned, because a state that names no
    score keeps the last state's.

## Decided while building

- **Beats are said as fractions** (¾ beat, 1½ beats), to the nearest
  twenty-fourth, because the sample's dotted rhythms read as "0.8" and "0.2"
  in decimals. Anything finer falls back to one decimal.
- **The name scales with the card**, by its height and width, up to 240 px, so
  a maximised window reads from across a room. Below 320 × 300 px the compact
  layout takes over; at 200 px the full card squashed its name into its time
  bar.
- **The setting lives on `CurrentTarget`** (`followScoreWhilePlaying`), beside
  `keepChosenWhenScoreOpens`, so the Settings panel and the follower reach it
  the same way. `TargetClockFollower` in the shell owns the follower and
  listens to the clock.
- **The tool reads the output latency** through a function its factory builds
  from `ToolServices::audio`, as the take recorder reads it from the shell.
- **Played through the microphone with a chord in the part**, the item is the
  chord and the shared target is its top note.
