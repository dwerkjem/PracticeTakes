## Context

`evaluation::judgeTiming` aligns performed onsets (keyboard chords or
microphone onsets) to the score's expected attacks, one moment per onset tick.
It returns, for each attack, the onset matched to it, if any, and the onsets
left over. A keyboard onset carries the pitches of the keys in its chord. A
microphone onset carries none.

## Decisions

### 1. Pitch is compared within the moments timing already matched

The moment alignment already settles the hard part: which played chord
answers which written one, through repeats, missed notes and stray sounds.
Within a matched moment, the written notes and the played pitches are compared
as sets:
1. Exact pitch matches pair first. Each is **correct** and takes the moment's
   timing verdict.
2. The written notes left over pair with the played pitches left over, nearest
   pitch first. Each such pair is a **wrong** note: what should have been
   played, beside what was.
3. Written notes still left are **missed**, and played pitches still left are
   **extra**, at that moment.

*Rejected:* aligning note by note with pitch in the cost. It doubles the search,
and it lets a pitch error at one moment drag the timing match of its
neighbours. Timing decides the moments; pitch then decides within them.

### 2. Ambiguity

In step 2, a played pitch that is equally near two written notes is paired
with the lower of them, and both results are marked **ambiguous**. So are any
wrong-note pairs in a moment whose leftover counts differ: three written and
two played leave a choice about which was missed.

### 3. Unmatched moments and onsets

A moment with no onset has every note **missed**. An onset with no moment has
every pitch **extra**, at its frame. A microphone onset has no pitches, so it
counts as one extra sound with an unknown pitch.

### 4. Without pitch

When the matched onset carries no pitches (the microphone, until #42), every
written note of the moment is **pitch not judged**: played at that time, pitch
unknown. That is honest about what the evidence says, and it fills in without
a code change once #42 gives microphone onsets their pitches.

### 5. The report

`NoteReport` holds a `NoteResult` for every written note:
- its `NoteRef`, part, tick and written pitch;
- its outcome: correct, wrong, missed or pitch not judged;
- the played pitch, for a wrong note;
- whether the pairing was ambiguous;
- the moment's timing verdict and error.

Beside it go the extra notes, each with its frame and its pitch if known, and
counts of each outcome. `matchNotes(timingReport)` builds it, and
`judgeNotes(take, part)` runs timing then matching.
