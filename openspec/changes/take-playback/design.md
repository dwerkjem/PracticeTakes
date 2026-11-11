## Context

The clock's renderer is the only thing that makes sound on the audio thread
(`RealtimeRenderer.h`: "whatever else comes to make a sound is rendered by the
clock"). It plays the score's notes through `ScoreSynth` from a
`PlaybackScore` that travels in the same slot as the table. A take's frames
are clock frames, at the take's scale and sample rate, so a take sits on the
clock's timeline already.

## Decisions

### 1. A take plays through the clock

Playing a take republishes the clock's table with the take as its sound, then
seeks to where the take started and plays without a count-in:

- **The table** is built at the take's scale and the device's rate, with no
  loop. Its end is moved to the take's last frame, so the clock stops where
  the take stopped.
- **The playback score** holds the take's keyboard notes on one extra track
  (an acoustic piano), with no score notes. The audible mask is that track
  alone.
- **The take's sound** is its microphone samples and the frame they start at,
  in the slot beside the table.

So the score cursor, the Score View and Now & Next follow the take as they
follow the score, and Pause, Stop and seeking work unchanged. A second
renderer, or a second timeline, was the alternative; `RealtimeRenderer.h`
rules both out.

### 2. The samples are the message thread's, read by pointer

The slot holds a `shared_ptr<const std::vector<float>>`, assigned only on the
message thread when it fills the slot the renderer is not reading. That is the
existing hand-off, so a slot is never refilled while it is in use. The audio
thread reads the raw pointer and copies at most one block's frames per block,
with a gain. Nothing is allocated, locked or freed there: the last reference
to an old buffer is dropped when the message thread refills that slot.

### 3. Rates: resampled once, before playing

A take recorded at 48 kHz on a device now at 44.1 kHz is resampled, linearly,
on the message thread when it is prepared, and its frames are scaled by the
ratio. Linear interpolation is enough for review listening and costs nothing
on the audio thread. A matching rate copies nothing.

### 4. The pedal, as it sounded

Keyboard notes are turned into note-ons and note-offs by `NoteSoundingTracker`,
the same rule the review judges note lengths by, with the reader's pedal
threshold:

- a key released under the pedal sounds until the pedal lifts;
- a note still held at the take's end stops there.

`PlaybackEvent` gains a velocity, so a take sounds with the touch it was
played with; zero keeps the score's fixed velocity.

### 5. A mode that ends with Stop

While a take plays, the clock remembers its own scale, loop, metronome and
mix. When the clock next stops, by Stop or by reaching the take's end, take
playback ends and those are put back. Pause keeps it, so Play resumes the
take. Recording a take, or a new score opening, ends it first.

### 6. Export renders off the audio thread

**Export Audio…** renders on the library's worker:

- the microphone samples, centred;
- the keyboard, through a `ScoreSynth` of its own loaded from the same
  SoundFont, in blocks of `ScoreSynth::renderFrames`;
- mixed with the same gains as playback, and written as a 24-bit stereo WAV.

A take with neither yields a file of silence of its length rather than
nothing.

### 7. What plays, where

- **Take Review** gets **Play** and **Stop** for the take under review. They
  are disabled when the take's score is not the open one: a session take of a
  score since replaced.
- **The Library** gets **Play**, which reviews the take (opening its score)
  and then plays it.
- **Both** offer **Export Audio…**.

## Risks / Trade-offs

- **Memory:** a three-minute take at 48 kHz is about 35 MB as floats, held
  while it plays. That is acceptable, and it is released when playback ends.
- **Linear resampling** dulls the top octave slightly when the rates differ;
  matching rates, the common case, are exact.
