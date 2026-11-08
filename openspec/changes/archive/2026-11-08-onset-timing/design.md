## Context

A `take::Take` holds:
- the microphone's audio, with the clock frame of its first sample;
- the keyboard's events, each with its clock frame;
- the score, the start tick and the scale;
- a copy of the clock's `ClockTable` (frames and ticks at the take's scale);
- the sample rate and the device's stated latencies.

`score::Score` gives every event's onset and duration in ticks, and each note's
tie links.

## Decisions

### 1. Expected attacks: one moment per onset tick, for one part

`expectedAttacksOf(score, part)` walks the part's measures and voices. It emits
one `ExpectedAttack` per distinct absolute tick (`measure.start +
event.onset`), carrying every note that starts there. Chords and simultaneous
voices merge into one moment. A note tied from an earlier note is left out, and
an event left with no notes makes no attack. Rests and grace notes make none.

The part is a parameter. The take keeps the whole score, and the review (#52)
chooses which part a take is judged against. A take is cheap to judge again
for another part, so no choice has to be made before it is recorded.

### 2. Keyboard onsets: note-ons gathered into chords

A note-on with velocity above zero is an onset at its frame. Note-ons within
30 ms of the first one in a group form one onset at the earliest frame, the
way a chord is played. Each onset keeps the pitches it holds, for #48. Its
strength is the loudest velocity, divided by 127.

### 3. Microphone onsets: spectral flux

The detector frames the audio with a 1024-sample Hann window and a 128-sample
hop (23 ms and 2.9 ms at 44.1 kHz). For each frame it takes the magnitude spectrum by a radix-2 FFT and
compresses it, log(1 + 10·|X|). The flux is the half-wave-rectified rise
between consecutive frames, summed over bins. Peaks are picked where the flux
exceeds the moving median over ±10 frames by an adaptive margin, and stands
above a floor. Picked peaks are at least 50 ms apart.

An onset's time is the hop's position refined by a parabola through the flux
peak, plus a fixed correction. Log-compressed flux peaks as soon as an attack
enters the window, so it leads by 0.277 of a window, measured on plucked tones
from 110 Hz to 1.76 kHz with fast and slow decays. Once corrected, every one is
placed within 5 samples, and the tests require 10 ms.

Only windows wholly inside the audio are judged. A take cut off mid-note, which
Finish does, would otherwise end with a false attack. Its strength
is the peak's height over the threshold, normalised into 0–1.

The FFT is a small iterative radix-2 transform here, not JUCE's. The code
stays JUCE-free and testable, and it runs on the message thread after a take,
never on the audio thread.

### 4. Alignment: dynamic programming, in order, within a window

Expected attacks (frames, from the take's table) and onsets (frames) are both
in order. The aligner minimises a cost:
- matching attack *i* to onset *j* costs |Δ| / window(i);
- leaving an attack unmatched costs 1;
- leaving an onset unmatched costs 0.6.

A match is only allowed when |Δ| is within the attack's window: half the gap
to its neighbouring attacks, at least the tolerance and at most 250 ms. The
match is monotonic, so two notes of the same pitch in a row each find their
own onset by time. Only pairs inside the window are considered, which keeps
the cost near linear.

Extra onsets cost less than missing attacks. A stray noise is then left over
rather than pulling a real note's match away from its onset.

### 5. Judgement and tolerance

For a matched attack:
- **error** = onset frame − attack frame − latency correction, in frames;
- in milliseconds, divided by the sample rate;
- in beats, divided by the frames per beat at that point of the table.

**On time** means |error| ≤ tolerance:

> tolerance = clamp(fraction × beat, minimum, maximum)

The defaults are 8% of a beat, at least 30 ms and at most 80 ms. That is 40 ms
at 120 bpm, the 30 ms floor for fast music, and the 80 ms cap for slow music.
Beyond the tolerance an attack is **early** or **late**. An attack whose onset
has strength below 0.2 is also marked **uncertain**. An attack with no onset is
**absent**. None of these thresholds is a judgement fixed in code: each is a
field of `TimingSettings`.

**Latency correction.** The performer hears the click late by the output
latency, so even an attack exactly on the beat as heard reaches the input late:
- keyboard onsets are corrected by the output latency;
- microphone onsets by the output and input latencies together.

The take's stated latencies are the default, until #38 measures real ones.

### 6. The report

`TimingReport` holds a `TimingResult` for every attack:
- the attack's tick and notes;
- the matched onset, if any;
- the error in milliseconds and beats;
- the verdict: early, on time, late or absent, and whether it is uncertain;
- the extra onsets.

A summary gives the counts and the mean and standard deviation of the error
over matched attacks. `judgeTake(take, part, settings)` is the one entry point:
it uses the keyboard when the take has keyboard events, and the microphone
otherwise.

## Risks / Trade-offs

- **Soft sung onsets** are weak in spectral flux and will often be uncertain or
  absent. That is reported honestly, not hidden. #42's pitch changes are the
  better evidence for legato singing, and will be added there.
- **The stated latencies can be wrong**, which biases every error by the same
  amount. #38 fixes it, and the bias is a parameter, not baked in.

## Implementation notes

- **The aligner keeps two rows of costs and one byte per state** for the way
  back, rather than a full table of doubles. A take of a few thousand attacks
  and onsets costs a few megabytes, once.
- **A window is never narrower than its attack's tolerance**, so a note
  between two very close neighbours can still be matched when it is on time.
- **The keyboard is preferred whenever a take has keyboard events.** A pianist
  who also sang is judged on the keys, which are exact. Judging the voice as
  well comes with #42's pitch, when a part's pitches can say which onset
  belongs to whom.
