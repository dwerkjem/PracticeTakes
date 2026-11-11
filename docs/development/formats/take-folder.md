# A saved take's folder

How a take is stored in the media library (`openspec/changes/media-library`,
decisions 3 and 4). The writer and reader are `library::writeTake` and
`library::readTake` in `src/platform/library/TakeFiles.h`; the manifest's codec
is `src/platform/library/TakeManifest.h`, and its tests state every rule below.

A saved take is user data, so this format must stay readable by every future
version.

## Where

`<library>/Takes/<yyyy-mm-dd HH-mm-ss> <song title>/`, the time being when the
take ended, in local time. A clash is numbered ` (2)`, ` (3)` and so on. The
title is cut to 60 characters and made legal for every file system; a take with
no title is named by its time alone.

## What is in it

| File | Holds | When |
|---|---|---|
| `take.json` | The manifest, below | Always; written last |
| `microphone.flac` | The microphone, mono, 24-bit, at the take's sample rate | When the take used the microphone and recorded anything |
| `score.mxl` or `score.musicxml` | A copy of the file the take's score was imported from | When that file was known |

The manifest is written last, as `take.json.partial`, and then renamed. **A
folder without `take.json` is not a take**: it is a save that was cut short, and
the listing passes it by. A save that fails removes what it had written.

The audio is the analysis tools' signal, after gain and mute: the samples the
take was judged from. `Take::audioStart` (`audioStart` below) is the clock frame
of its first sample.

The score copy is the take's own. The take is always judged against it, never
against the song in `Songs/`, so a song edited or removed afterwards changes
nothing about its takes. A take without a score copy cannot be loaded
(`library.take.error.no-score`).

## `take.json`

A JSON object. Frames are clock frames at the take's own sample rate: frame 0 is
the start point the take was counted in to, so a count-in is negative.

| Field | Type | Meaning |
|---|---|---|
| `formatVersion` | string `"major.minor"` | `"1.0"` today |
| `recorded` | integer | When it ended, in milliseconds since 1970 UTC |
| `scoreTitle` | string | The score's title, for display |
| `scoreFile` | string | The score copy's file name beside the manifest. A name only: one with `/`, `\` or `:` in it is ignored |
| `startTick` | integer | The score tick it was started from |
| `scalePercent` | integer | The tempo scale, 5–200; out of range reads as 100 |
| `sampleRate` | number | Hertz; must be above zero |
| `referenceHz` | number | A4 when it was sung; out of range reads as 440 |
| `inputLatencyFrames`, `outputLatencyFrames` | integer | The device's stated latencies |
| `usedMicrophone`, `usedKeyboard` | boolean | What it recorded from |
| `audioStart` | integer | The clock frame of the first audio sample |
| `countInFrames` | integer | How much of the audio was the count-in |
| `lastFrame` | integer | The last clock frame the take reached |
| `stoppedInBar` | integer, optional | The zero-based bar a stopped-early take stopped in |
| `state` | string | `completed`, `stoppedEarly` or `failed` |
| `failureReason` | string, optional | Why it failed |
| `loopStartTick`, `loopEndTick` | integer | The loop played; equal for none |
| `keyboard` | array | One `[frame, kind, channel, note, velocity, pedal]` per event; `kind` is `on`, `off` or `pedal` |

**Not stored, rebuilt:** the clock table, by `transport::buildTable` from the
score copy, `scalePercent`, `sampleRate` and the loop; and the lifecycle, by
replaying the events that end in `state`.

**What is refused, and what falls back.** A document that is not an object, has
no readable `formatVersion`, `state` or `sampleRate`, names a state that is never
saved (`cancelled`, or a live one), or is over 32 MB is refused. A field that is
present but out of range falls back to its default, and a keyboard event that
does not read is dropped, because the rest of the take is still the player's.

## Versions

A reader reads every version up to its own major and refuses a newer major
(`library.take.error.newer`), listing the take as needing a newer application
rather than misreading it. A newer minor version may add fields, which an older
reader ignores. Changing what an existing field means needs a new major version.
