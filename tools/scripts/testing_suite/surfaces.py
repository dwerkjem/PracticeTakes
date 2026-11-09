#!/usr/bin/env python3
"""What the testing suite verifies, as data.

A surface is one approved application state plus the questions to ask about it.
Surfaces are declarative on purpose: adding one is an edit to this list, not a
change to the suite, and the list is the reviewable record of what gets verified
before a release.

Every surface is scored on the same three axes, so runs stay comparable — you
can see that presentation regressed on the tuner between two versions, which a
bespoke question set per surface could never tell you. Surfaces may add their
own questions, recorded separately so they do not dilute that comparable core.

Questions come in two kinds, and the difference decides which pass asks them. A
question about *appearance* can be answered from a captured image, so it belongs
to the unattended capture and the later grid review. A question about
*behaviour* — does the tuner react to sound, does the divider drag, did the
layout survive a restart — cannot be answered from a still image at all, and is
marked `behavioural` so the short attended pass asks it against a live
application instead. Getting that split wrong is how a screenshot workflow
quietly stops verifying that anything works.

Standard library only.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import approved_states

# The three fixed axes, asked for every surface in this order.
FIXED_AXES: tuple[Question, ...]  # forward reference, defined below

QUICK = "quick"
FULL = "full"

# Titles the application gives its other top-level windows, from
# src/application/shell/ui/ShellWindowTitles.h. A surface does not name its
# window: the manifest says which window each state shows. These are here for
# the code and tests that look for one window in particular, and `validate`
# checks them against the manifest.
SETTINGS_WINDOW = "Settings"
FEEDBACK_WINDOW = "Send feedback"
SCORE_IMPORT_WINDOW = "Score import"

# The palettes a run captures each surface in. A capture dimension rather than a
# property of a surface, for the same reason resolution is: every surface exists
# in both palettes, and folding the theme into the surface list would double it.
DARK = "dark"
LIGHT = "light"
THEMES = (DARK, LIGHT)
DEFAULT_THEMES = THEMES

# The resolutions a run captures each surface at. Configuration rather than a
# fixed list: `--resolutions` overrides it, and the set a run covered is
# recorded with the run so two runs covering different sets are comparable on
# purpose rather than by accident.
DEFAULT_GEOMETRY = "default"

# "tiny" is 640x480 -- smaller than a window manager would let a user drag it,
# and deliberately so. Most of what a run finds is a layout out of room, and
# this is the size at which a docked tool has to choose what to drop.
# Every geometry a run may be asked for. "slim" and "squat" put a *pane* below
# the size its tool draws at, which the others cannot: they shrink the window,
# and a single docked tool in a small window still has a roomy pane.
SWEEP_GEOMETRIES = (DEFAULT_GEOMETRY, "constrained", "tiny", "maximised", "slim", "squat")

# What a run covers when nobody says otherwise. Deliberately not all of them --
# the two compact shapes are asked for when the compact displays are what is
# being looked at, and would otherwise add two more images per surface to every
# full sweep.
DEFAULT_RESOLUTIONS = (DEFAULT_GEOMETRY, "constrained", "tiny", "maximised")


@dataclass(frozen=True)
class Question:
    id: str
    prompt: str

    # True when only a live application can answer it. Asked by the attended
    # pass; never inferred from an image.
    behavioural: bool = False


FIXED_AXES = (
    Question("looks-correct", "Does it look correct?"),
    Question("looks-good", "Does it look well-presented?"),
    Question("works", "Does it work?"),
)


def derived_fields(state_id: str) -> dict:
    """What the manifest says about a state, in the Surface's own terms.

    A state the application does not declare gets the empty values, so a test
    can describe a surface of a made-up state; `validate` reports such a state
    in the real list.
    """
    state = approved_states.manifest().find(state_id)

    if state is None:
        return {"tools": (), "presentation": "", "window_title": "", "fixed_geometry": False}

    if state.presentation == "none":
        presentation = ""
    elif state.presentation == "docked" and state.arrangement != "single":
        presentation = state.arrangement
    else:
        presentation = state.presentation

    return {
        "tools": state.tools,
        "presentation": presentation,
        "window_title": state.window_title,
        "fixed_geometry": state.is_about_its_geometry,
    }


@dataclass(frozen=True)
class Surface:
    """One thing a tester is asked to look at."""

    # The approved state the application is put into. Must exist in
    # src/application/testcontrol/ApprovedWindowStates.cpp; the harness checks
    # this against `list-states` before a run rather than discovering it
    # halfway through.
    state: str

    title: str

    # Which run modes include this surface. Quick mode is a strict subset of
    # full, which `validate` enforces.
    modes: frozenset[str]

    # Questions specific to this surface, asked after the three fixed axes.
    extras: tuple[Question, ...] = ()

    # Restart the application before this surface. Needed for anything that
    # verifies state surviving a relaunch, which cannot be checked inside one
    # process.
    restart_before: bool = False

    # Capture this surface with a real input device, against the harness's
    # default of `--no-audio-input`.
    #
    # Almost nothing needs one: the generated tone gives the analysis tools a
    # signal without a device, and declining the device is what stops a capture
    # run taking the microphone from a live application (#176). But a surface
    # that is *about* the device layer cannot be photographed by an instance
    # that has no device -- the Settings input chooser came back reading
    # "<< none >>", with no channel list and no advanced settings, and its
    # question ("did switching take effect?") could only ever answer no.
    #
    # The worker restarts into this mode for the surface and back out after, so
    # the microphone is held for one surface rather than for the whole run.
    needs_input_device: bool = False

    # Something the tester must do by hand before answering, because it is an
    # interaction rather than a state. The harness prompts and waits; it does
    # not simulate it.
    instruction: str = ""

    # This surface *is about* its window size, so the geometry sweep must leave
    # it alone. Sweeping it would resize the window to each geometry in turn and
    # so destroy the very thing under test -- which is exactly what happened:
    # narrow-window was reported "not narrow" and fullscreen "not fullscreen",
    # because the sweep set them both back to the default size.
    #
    # From the manifest when left out, as `tools`, `presentation` and
    # `window_title` are below: a state at any geometry but the normal one is
    # about its size.
    fixed_geometry: bool | None = None

    # What this surface is, for filtering a review by it. A grid of a hundred
    # captures is only reviewable if you can ask it for "every settings window
    # in the light palette" or "everything with three tools open" and approve
    # those together, so these are data rather than something inferred from the
    # title by eye.
    #
    # `tools` names what the state opens, `presentation` how they are shown, and
    # `area` what part of the application the surface belongs to.
    #
    # `tools` and `presentation` come from the manifest when left out, which in
    # SURFACES they always are. `presentation` is "docked" or "floating", or the
    # arrangement -- "split" or "tabbed" -- when several tools are docked
    # together; empty when the state opens no tool.
    tools: tuple[str, ...] | None = None
    presentation: str | None = None
    area: str = "workspace"

    # Seconds to let the application run before capturing it.
    #
    # A tool drawing a history -- the tuner's graph, the spectrogram's
    # waterfall -- has only as much to draw as the time it has been open, so a
    # capture taken the instant a state settles shows a sliver. Surfaces fed a
    # tone wait, so the picture is of a tool that has been listening rather than
    # one that just started.
    warmup_seconds: float = 0.0

    # The title of the top-level window this surface is about, when that is not
    # the main window. Settings, feedback, and a floating tool are each their
    # own X window, and a capture that does not name one photographs whichever
    # window the window manager lists first -- which is the main one. A real run
    # caught this: the settings surface came back byte-identical to the empty
    # shell, at both resolutions.
    #
    # These surfaces are also captured once rather than at every resolution: the
    # geometry command resizes the *main* window, so sweeping would produce the
    # same image under three different labels.
    #
    # From the manifest, which derives it from what the state opens, when left
    # out.
    window_title: str | None = None

    def __post_init__(self) -> None:
        # Fill in what the application already declares about the state. A
        # value given explicitly wins, which is how tests describe surfaces of
        # states the application does not have; `validate` checks no surface in
        # SURFACES contradicts the manifest.
        derived = derived_fields(self.state)

        for name, value in derived.items():
            if getattr(self, name) is None:
                object.__setattr__(self, name, value)


# The surface set. Ordered as a run presents them: cheapest and most
# fundamental first, so a broken build fails early rather than after a tester
# has answered twenty questions.
SURFACES: tuple[Surface, ...] = (
    Surface(
        state="empty",
        title="The shell with no tool open",
        area="shell",
        modes=frozenset({QUICK, FULL}),
    ),
    Surface(
        state="tuner-docked",
        title="The tuner, docked",
        modes=frozenset({QUICK, FULL}),
        extras=(
            Question(
                "live-input",
                "Does the tuner respond to sound from the microphone?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="settings-open",
        title="The settings window",
        area="settings",
        modes=frozenset({QUICK, FULL}),
    ),
    # The tuner is not one surface. It has three ways of drawing the same
    # analysis, an expandable settings panel, and it looks different depending
    # on what it hears -- and a screenshot of one of those says nothing about
    # the others. Each of these feeds a synthetic tone, so the capture shows the
    # tool working rather than the tool waiting.
    Surface(
        state="tuner-in-tune",
        warmup_seconds=2.5,
        title="The tuner, dead in tune",
        modes=frozenset({QUICK, FULL}),
    ),
    Surface(
        state="tuner-sharp",
        warmup_seconds=2.5,
        title="The tuner, reading sharp",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="tuner-flat",
        warmup_seconds=2.5,
        title="The tuner, reading flat",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="tuner-bar",
        warmup_seconds=2.5,
        title="The tuner in its bar view",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="tuner-meter",
        warmup_seconds=2.5,
        title="The tuner in its meter view",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="tuner-advanced",
        warmup_seconds=2.5,
        title="The tuner with advanced settings open",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="spectrogram-tone",
        warmup_seconds=2.5,
        title="The spectrogram with a tone in it",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="harmonics-tone",
        warmup_seconds=2.5,
        title="The analyser reading a sustained pitch",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="all-tools-tone",
        warmup_seconds=2.5,
        title="Every tool analysing the same tone",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="spectrogram-docked",
        title="The spectrogram, docked",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "live-input",
                "Does the spectrogram respond to sound from the microphone?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="harmonics-docked",
        title="The harmonic analyser, docked",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "live-input",
                "Does the analyser respond to sound from the microphone?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="tuner-floating",
        title="The tuner in a floating window",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "moveable", "Can the floating window be moved and resized?", behavioural=True
            ),
        ),
    ),
    Surface(
        state="spectrogram-floating",
        title="The spectrogram in a floating window",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="harmonics-floating",
        title="The harmonic analyser in a floating window",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="tuner-harmonics-split",
        title="The tuner and the analyser tiled",
        modes=frozenset({FULL}),
        extras=(
            Question("divider", "Can the divider between the two be dragged?", behavioural=True),
        ),
    ),
    Surface(
        state="two-tuners-split",
        title="Two tuners tiled side by side",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "independent",
                "Does changing one tuner's display mode leave the other as it was?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="three-tools-tabbed",
        title="All three tools sharing a tab strip",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "switching",
                "Does switching tabs show each of the three?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="two-tools-split",
        title="Two tools tiled side by side",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "divider", "Can the divider between the two be dragged?", behavioural=True
            ),
        ),
    ),
    Surface(
        state="two-tools-tabbed",
        title="Two tools sharing a tab strip",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "switching", "Does switching tabs show the other tool?", behavioural=True
            ),
        ),
    ),
    Surface(
        state="all-tools-docked",
        title="Every tool open at once",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "all-live",
                "Do all three audio tools respond to sound at the same time?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="narrow-window",
        title="A narrow window with the collapsed menu",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "hamburger",
                "Does the collapsed menu open and list the same actions?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="narrow-empty",
        title="A narrow window with no tool open",
        area="shell",
        modes=frozenset({FULL}),
        extras=(
            # What a new user sees first, which used to be a blank window with
            # nothing to say a score could be opened. Asked here rather than on
            # `empty` because a narrow window is where the guidance is likeliest
            # to be clipped -- and because the review tests use `empty` as their
            # surface that owes only the three fixed axes.
            Question(
                "says-how-to-start",
                "Does the window say how to open a score, and where the tools are?",
            ),
        ),
    ),
    Surface(
        state="fullscreen",
        title="Fullscreen",
        modes=frozenset({FULL}),
        extras=(
            Question("escape", "Does Escape or F11 leave fullscreen?", behavioural=True),
        ),
    ),
    Surface(
        state="microphone-muted",
        title="Global microphone mute engaged",
        area="audio",
        modes=frozenset({FULL}),
        extras=(
            Question("mute-obvious", "Is it obvious at a glance that input is muted?"),
            Question(
                "mute-silences", "Do the tools stop responding to sound?", behavioural=True
            ),
        ),
    ),
    Surface(
        state="muted-all-tools",
        title="Every tool at once, microphone muted",
        area="audio",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "all-silent",
                "Do all three stop responding to sound together?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="microphone-warning",
        title="The no-usable-input warning",
        area="audio",
        modes=frozenset({FULL}),
        extras=(
            Question("actionable", "Does the warning say what to do about it?"),
            Question(
                "dismissable", "Can it be dismissed?", behavioural=True
            ),
        ),
    ),
    Surface(
        state="settings-audio-device",
        title="Settings: audio device selection",
        area="settings",
        modes=frozenset({FULL}),
        needs_input_device=True,
        instruction="Switch to a different input device, then back.",
        extras=(
            Question(
                "switching",
                "Did switching device take effect without a restart?",
                behavioural=True,
            ),
        ),
    ),
    # The only surface that looks at the MIDI capture path at all. What it can
    # show without hardware is the empty case -- no keyboard connected -- which
    # is worth a picture in its own right, because it is what a user sees before
    # they switch their piano on and it has to tell them what to do. The
    # connected case needs a keyboard and is task 7.5 in the change, not a
    # capture.
    Surface(
        state="settings-midi",
        title="Settings: MIDI keyboard selection",
        area="settings",
        modes=frozenset({FULL}),
        instruction=(
            "If you have a MIDI keyboard, connect it, choose it, and play a few notes "
            "with and without the sustain pedal. If you do not, answer for the "
            "disconnected state only and say so."
        ),
        extras=(
            Question(
                "empty-state-helpful",
                "With no keyboard connected, does it say so and say what to do about it?",
            ),
            Question(
                "notes-appear",
                "With a keyboard connected, do played notes appear with their velocities?",
                behavioural=True,
            ),
            Question(
                "pedal-appears",
                "Does the sustain pedal show as going down and up?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="settings-appearance",
        title="Settings: appearance and theme",
        area="settings",
        modes=frozenset({FULL}),
        instruction="Change the theme, and look at a tool with it applied.",
        extras=(Question(
                "theme-applies", "Did the theme apply everywhere?", behavioural=True
            ),),
    ),
    Surface(
        state="settings-open",
        title="Settings: import and export round trip",
        area="settings",
        modes=frozenset({FULL}),
        instruction=(
            "Export settings to a file, change something, then import the file back."
        ),
        extras=(
            Question(
                "round-trip",
                "Did importing restore exactly what was exported?",
                behavioural=True,
            ),
            Question(
                "reports",
                "Did it say clearly whether the import succeeded?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="settings-practice",
        title="Settings: practice presets",
        area="settings",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="settings-reset",
        title="Settings: reset and support",
        area="settings",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="feedback-over-tool",
        title="The feedback form over a tool in use",
        area="feedback",
        modes=frozenset({FULL}),
    ),
    Surface(
        state="feedback-open",
        title="The feedback form",
        area="feedback",
        modes=frozenset({FULL}),
    ),
    # The score import summary. The only surfaces that depend on a *document*:
    # the window has nothing to show until a file has been read, so the state
    # imports a committed fixture on the way in, through the same job the File
    # menu uses.
    #
    # `warmup_seconds` here waits for an import rather than for a tool's
    # drawing history, which is what every other surface using it waits for.
    # The import is deliberately asynchronous -- the result crosses back to the
    # message thread -- so a capture taken the instant the state settles is a
    # picture of "Reading ...", not of the report. The fixtures are small
    # enough to read in milliseconds; the seconds here are slack for a loaded
    # machine, and an in-progress capture is obviously wrong in the review grid
    # rather than subtly wrong.
    # A score being imported: the moment between choosing a file and its
    # report. Held by the state rather than waited for, since a small file is
    # read in milliseconds and would otherwise only ever be photographed by
    # accident.
    Surface(
        state="score-import-in-progress",
        title="The score import window while a score is being read",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=1.0,
        extras=(
            Question(
                "names-the-file",
                "Does it say which file is being read -- Mahler - Die zwei blauen Augen.mxl?",
            ),
            Question(
                "reads-as-in-progress",
                "Is it plainly an import still in progress, rather than a finished report "
                "with nothing in it?",
            ),
            Question(
                "progress-becomes-the-report",
                "This window starts held at 'Reading...' on purpose, for the picture. Now use "
                "File > Open sample score: does this same window turn into the import report, "
                "without a second window appearing?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="score-import-diagnostics",
        title="The score import summary, with diagnostics",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "diagnostics-name-their-element",
                "Does every diagnostic say which element it is about?",
            ),
            Question(
                "repeated-message-still-legible",
                "Where several diagnostics share one message, can you still tell the rows apart?",
            ),
            Question(
                "severities-separated",
                "Are the dropped, repaired, and ignored groups each under their own heading?",
            ),
        ),
    ),
    # Kept alongside the diagnostic surface, which covers strictly more of the
    # renderer, because "Nothing was dropped or repaired." is a branch of its
    # own: an empty diagnostics list and a failure to report diagnostics look
    # identical on screen, and that sentence is the only thing distinguishing
    # them.
    Surface(
        state="score-import-clean",
        title="The score import summary for a clean import",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "says-nothing-was-dropped",
                "Does it say explicitly that nothing was dropped, rather than showing an "
                "empty list?",
            ),
            Question(
                "metadata-complete",
                "Are the work, composer, parts, length, and tempo all shown?",
            ),
            # The capture is of the summary, which sits in front; this is about
            # the main window behind it. Opening a score used to stop at this
            # report, and closing it changed nothing on screen.
            Question(
                "score-is-shown",
                "Close this window. Is the score now shown in the Score View?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="score-view-empty",
        title="The score view with no score open",
        area="score",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "says-what-to-do",
                "Does it name the command that opens a score, rather than showing an empty "
                "staff?",
            ),
        ),
    ),
    # The surfaces the renderer exists for. Everything about this tool is
    # visual: the layout is a value and is unit tested down to which line each
    # note sits on, and whether the result is *readable* is a question only an
    # image can answer.
    #
    # These are the first states that open a score into a tool. That was
    # impossible until the import summary could be suppressed -- see finding 7
    # in docs/development/architecture/score-model-findings.md -- and the same
    # asynchronous import applies here as to the two summary surfaces above, so
    # they carry the same warmup.
    Surface(
        state="score-view-notation",
        title="The score view drawing a staff",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "reads-as-music",
                "Does it read as music -- five staff lines, a recognisable clef, and noteheads "
                "sitting on lines and in spaces rather than between them?",
            ),
            Question(
                "two-sharps-after-the-clef",
                "Are there exactly two sharps between the clef and the first note, and is the "
                "time signature after them?",
            ),
            Question(
                "accidentals-once-each",
                "Bar 2 is a flat, the same flat again, then a natural: is the sign drawn on the "
                "first and third of those and not on the second?",
            ),
            Question(
                "stems-and-flags-attached",
                "Is every stem touching its notehead, on the correct side, with its flags "
                "curling away from the head?",
            ),
            Question(
                "beams-join-their-stems",
                "Bar 1 ends with two eighths and bar 3 holds two sixteenths: is each pair "
                "joined by a bar across the ends of their stems, with the sixteenths carrying "
                "two bars and the eighths one, and does every bar meet its stems flush rather "
                "than stopping short of them?",
            ),
            Question(
                "an-eighth-is-not-a-quarter",
                "Can you tell every eighth and sixteenth from a quarter note -- by a flag or a "
                "beam -- without counting anything?",
            ),
            Question(
                "text-clear-of-notes",
                "Is every lyric, dynamic, tempo mark and bar number clear of the notes -- not "
                "touching a notehead, stem, beam or ledger line -- and does each kind of text "
                "sit on one straight line across its row of music?",
            ),
            Question(
                "nothing-collides",
                "Do any two symbols overlap -- an accidental over a notehead, a lyric over a "
                "stem, a dot over a barline?",
            ),
            Question(
                "clef-change-visible",
                "Is the change to a bass clef part way through drawn, and are the notes after "
                "it low on the staff rather than high?",
            ),
            Question(
                "lyrics-under-their-notes",
                "Is each syllable under the note it belongs to, on one row, with a hyphen only "
                "where a word continues?",
            ),
            Question(
                "notices-are-readable",
                "Does the strip at the bottom say what is not drawn, and that beam groups came "
                "from the time signature rather than from the file, in sentences rather than "
                "in codes?",
            ),
        ),
    ),
    # A piano part, which the score view used to draw as its right hand alone.
    # The fixture's four bars each exist for one thing a grand staff has to get
    # right; the questions name them bar by bar.
    Surface(
        state="score-view-grand-staff",
        title="The score view drawing a piano part as a grand staff",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "both-staves-braced",
                "Are both staves drawn, treble above bass, joined at the left by a brace?",
            ),
            Question(
                "barlines-through-both",
                "Does every barline run through both staves and the gap between them, rather "
                "than stopping at each staff?",
            ),
            Question(
                "beats-line-up",
                "In bar 1, are the left hand's two half notes directly under the right hand's "
                "first and third quarter notes?",
            ),
            Question(
                "sharp-moves-both-hands",
                "In bar 2, the right hand's first note has a sharp: is the left hand's first "
                "note still directly under it, and does the left hand's second eighth fall "
                "between the right hand's notes rather than under one?",
            ),
            Question(
                "lower-clef-change-only",
                "At bar 3, does only the lower staff change to a treble clef, with the upper "
                "staff's music still lined up with it?",
            ),
            Question(
                "nothing-collides",
                "Do any two symbols overlap anywhere on either staff?",
            ),
        ),
    ),
    # The fixture the engraving-polish change is checked against. Captured
    # before any of that change landed, so each question below starts out
    # answered "no" and the reference is retaken, deliberately, at the end of
    # each tier that changes it (tasks 4.1). Bar by bar, like the grand staff.
    Surface(
        state="score-view-engraving",
        title="The score view drawing the engraving fixture",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "chord-accidentals-stack",
                "Bar 1 opens with a three-note chord with a sharp on every note: are the "
                "sharps in columns, none overlapping another, each level with its own note?",
            ),
            Question(
                "seconds-either-side-of-the-stem",
                "Bar 1's second chord has its stem up and bar 2's two chords have theirs down: "
                "in each, are the two notes a step apart on opposite sides of the stem, both "
                "touching it -- the upper note to the right when the stem is up, the lower "
                "note to the left when it is down?",
            ),
            Question(
                "accidental-on-its-own-note",
                "In those same three chords, is every sharp clear of both noteheads and "
                "plainly in front of the note it belongs to, rather than inside the other "
                "note?",
            ),
            Question(
                "tie-across-the-break",
                "The last note of bar 4 is tied to the first of bar 5, across the end of the "
                "line: is the tie drawn on both lines -- leaving the note at the end of one "
                "and arriving at the note at the start of the next?",
            ),
            Question(
                "endings-bracketed",
                "Are bars 7 and 8 under brackets labelled 1. and 2., the first closed at the "
                "repeat sign and the second left open?",
            ),
            Question(
                "jumps-shown",
                "Is there a segno sign above the start of bar 3, and \"D.S. al Coda\" above "
                "the end of the last bar, both clear of the notes and of any other text?",
            ),
            Question(
                "rests-condensed",
                "Are bars 9 to 12 drawn as one bar holding a thick horizontal bar with a 4 "
                "above the staff, rather than as four bars of rest?",
            ),
            Question(
                "lyrics-below-ledger-lines",
                "In bar 3 the notes hang below the staff on ledger lines: are the lyrics under "
                "them clear of every ledger line and notehead?",
            ),
            Question(
                "nothing-collides",
                "Do any two symbols overlap anywhere -- an accidental, notehead, stem, ledger "
                "line, lyric or barline?",
            ),
        ),
    ),
    # The score cursor at a known place (score-cursor): the clock moved to bar
    # 6 and left there, so the picture is the same every run.
    Surface(
        state="score-view-cursor",
        title="The score cursor at bar 6",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=5.0,
        extras=(
            Question(
                "cursor-on-the-bar",
                "Is a coloured band drawn across the system at the first beat of bar 6, over "
                "its notes?",
            ),
            Question(
                "marks-legible",
                "Are the sounding notes tinted and the next two onsets ringed, with every "
                "notehead, accidental and stem still readable through the marks?",
            ),
            Question(
                "follows-while-playing",
                "Press Play: does the cursor move with the music, the page turn by itself at "
                "the next page, and scrolling by hand stop that until Follow is pressed?",
                behavioural=True,
            ),
            Question(
                "click-to-seek",
                "Click a note in another bar: does the cursor jump there, and does Play start "
                "from it?",
                behavioural=True,
            ),
        ),
    ),
    # A take recording from the tone (takes).
    Surface(
        state="take-recording",
        title="A take being recorded",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=5.0,
        extras=(
            Question(
                "record-in-red",
                "Does the transport show Finish in red, and a readout saying Recording with a "
                "time, while the tempo and Loop are greyed out?",
            ),
            Question(
                "take-ends-kept",
                "Press Finish: does the readout say the take stopped in a bar, with its length, "
                "and then go back to the bar and beat?",
                behavioural=True,
            ),
            Question(
                "real-take",
                "With a real microphone and keyboard: does a take record both, and does muting "
                "the microphone mid-take keep it going?",
                behavioural=True,
            ),
        ),
    ),
    # A synthetic pianist's take under review (take-review).
    Surface(
        state="take-review",
        title="A take under review",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "marks-on-notes",
                "Are the piano's notes ringed green, with a few filled red and labelled with "
                "the key played, a few ringed grey, and small orange arrows beside some?",
            ),
            Question(
                "voice-unmarked",
                "Is the voice's staff free of marks, since the take was judged against the "
                "piano?",
            ),
            Question(
                "extras-above",
                "Is there a red cross above the staff where an extra key was played?",
            ),
            Question(
                "list-beside",
                "Does the Take Review list say the take stopped in bar 8, name the piano, "
                "and list results in time order with bar, beat, pitch and outcome?",
            ),
            Question(
                "row-moves-clock",
                "Click a red row: does the score cursor move to that note?",
                behavioural=True,
            ),
        ),
    ),
    # The Mix popup (score-playback).
    Surface(
        state="playback-mix",
        title="The Mix popup",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "mix-rows",
                "Does the popup under Mix show Play the score ticked, a volume, and rows for "
                "Voice, Piano right hand and Piano left hand, the right hand's Mute on?",
            ),
            Question(
                "hear-the-score",
                "Press Play with speakers on: does the score play in time with the clicks, the "
                "piano's right hand silent, and does Solo on the voice leave only the voice?",
                behavioural=True,
            ),
        ),
    ),
    # The tuner against the shared target (system-wide-target-note).
    Surface(
        state="tuner-target",
        title="The tuner with a target",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "target-line",
                "Above the frequency, does a line name the target from the sample song and say "
                "how far flat of it the tone is, in cents?",
            ),
        ),
    ),
    Surface(
        state="tuner-target-a442",
        title="The tuner with a target, at A442",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "flatter-at-442",
                "Is the same tone now further flat of the target than at A440, and does Settings "
                "> Practice show Reference pitch 442 Hz?",
            ),
        ),
    ),
    # The target picker (target-note-picker).
    Surface(
        state="target-picker",
        title="The target picker",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "every-way",
                "Does the picker under the target line show a keyboard, a grand staff with the "
                "target's notehead and flat, natural and sharp beside it, a name field, Use the "
                "note I'm on, and the four step buttons?",
            ),
            Question(
                "choosing-works",
                "Click a key, a staff line, type Bb3, hold a note and use it, and step: does the "
                "tuner's target line follow each at once?",
                behavioural=True,
            ),
        ),
    ),
    # The Note View (note-view).
    Surface(
        state="note-view",
        title="The Note View",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "key-and-target",
                "Is the key being sung lit, shaded between green and amber, and the target's "
                "key ringed, distinguishable at a glance?",
            ),
            Question(
                "staff-and-trail",
                "Does the staff show the sung note and the target side by side, and the trail "
                "show the tone's wobble as a continuous line above the keys?",
            ),
            Question(
                "keys-held",
                "With a MIDI keyboard, are held keys lit blue, and does a slide sung from one "
                "note to another draw as one line?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="note-view-empty",
        title="The Note View with nothing sung",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "says-what-to-do",
                "Does it ask for a note to be sung or played, and say why there is no target?",
            ),
        ),
    ),
    # The Take Review tool before any take.
    Surface(
        state="take-review-empty",
        title="Take Review with no take",
        area="score",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "says-how",
                "Does it say there is no take yet, and how to record one?",
            ),
        ),
    ),
    # The practice clock playing the sample (practice-clock). The position
    # moves, so a question asks what the transport says rather than which bar.
    Surface(
        state="transport-playing",
        title="The practice clock playing the sample score",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=5.0,
        extras=(
            Question(
                "transport-in-strip",
                "Does the control strip at the top show the transport between the menus and "
                "the microphone -- Pause, Stop, a bar and beat, a tempo of 100%, Click and Loop "
                "-- with nothing cut off or overlapping?",
            ),
            Question(
                "transport-says-playing",
                "Does the button read Pause, and the readout name a bar and beat after the first "
                "beat of bar 1, showing the clock has been running?",
            ),
            Question(
                "click-in-time",
                "With speakers on, does the metronome click steadily on each beat, louder on the "
                "first of each bar, and keep sounding when the microphone is muted?",
                behavioural=True,
            ),
        ),
    ),
    # The score the application ships, opened the way a first launch opens it.
    # Real exporter output with far more in it than the hand-written fixtures,
    # so this is the picture that shows what the renderer does with an actual
    # song -- including what it says it leaves out.
    Surface(
        state="score-view-sample",
        title="The score view showing the sample score",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "sample-is-the-song",
                "Does the top of the view name the song cycle, Lieder eines fahrenden "
                "Gesellen, and both parts, Voice and Piano?",
            ),
            Question(
                "sample-reads-as-music",
                "Does the first system read as music -- clef, key, time signature, notes "
                "with stems and beams -- rather than as overlapping or misplaced symbols?",
            ),
            Question(
                "voice-over-braced-piano",
                "Is the voice drawn above the piano, with only the piano's two staves joined by "
                "a brace?",
            ),
            Question(
                "barlines-break-between-parts",
                "Do the barlines run through both piano staves but stop at the voice's staff, "
                "rather than running from the voice down through the piano?",
            ),
            Question(
                "parts-line-up",
                "In the first full bar, are the voice's notes directly above the piano notes "
                "played at the same moment?",
            ),
            Question(
                "text-clear-of-notes",
                "Is every lyric, dynamic, tempo mark and bar number clear of the notes -- not "
                "touching a notehead, stem, beam or ledger line -- and does each kind of text "
                "sit on one straight line across its row of music?",
            ),
            Question(
                "tempo-once",
                "Is the tempo mark (Alla Marcia) written once, above the voice, rather than "
                "again above the piano?",
            ),
            Question(
                "sample-opens-from-empty-window",
                "Close every tool, then click Open the sample score in the empty window: does "
                "the Score View open showing this song?",
                behavioural=True,
            ),
        ),
    ),
    # The sample in each of the Score View's three views. One surface per
    # view, because a picture of the default says nothing about the other two.
    Surface(
        state="score-view-page",
        title="The score view in Page view",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "page-edge",
                "Is the music on a page with a visible edge, lighter than what surrounds it, "
                "with \"Page 1 of\" a number above it?",
            ),
            Question(
                "whole-systems-on-the-page",
                "Is every line of music on the page whole, with none cut off at the page's "
                "foot?",
            ),
            Question(
                "page-turns",
                "Click \u203a, then press Page Down, then Home: does each show the right page "
                "from its top, with no bar split between two pages?",
                behavioural=True,
            ),
            Question(
                "enlarge-keeps-the-place",
                "On page 2, click +: is the bar that began the page still on screen, and does "
                "the page count go up?",
                behavioural=True,
            ),
        ),
    ),
    # The song with its voice hidden from the Parts menu
    # (openspec/changes/archive/2026-11-06-score-part-selection).
    Surface(
        state="score-view-part-hidden",
        title="The score view with a part hidden",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "only-the-piano",
                "Is only the piano drawn -- two braced staves, no voice staff above them -- "
                "and does the title strip name only Piano?",
            ),
            Question(
                "piano-fills-the-lines",
                "Do the piano's lines run the full width with more bars on each than when "
                "the voice is shown, rather than leaving a gap where the voice was?",
            ),
            Question(
                "tempo-and-name-on-the-piano",
                "Is the tempo mark (Alla Marcia) written above the piano, and is the piano "
                "named beside its staves on the first line?",
            ),
            Question(
                "parts-menu",
                "Open the ... menu, then Parts: is Voice unticked and Piano ticked and greyed "
                "out? Tick Voice: does it come back with the same bar still in view?",
                behavioural=True,
            ),
            Question(
                "parts-remembered",
                "Hide the voice again and restart the application: is the voice still hidden "
                "in this Score View?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="score-view-vertical",
        title="The score view in Vertical scroll",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "vertical-as-before",
                "Is the score one column of lines of music with only a vertical scrollbar, as "
                "the Score View looked before it had views?",
            ),
            Question(
                "size-controls",
                "Click +, then \u2212, then press Ctrl (or Cmd) and 0: does the music grow, "
                "shrink, and return to 100%, with the percentage between the buttons following?",
                behavioural=True,
            ),
            Question(
                "view-remembered",
                "Choose Horizontal scroll from the ... menu and restart the application: does "
                "the Score View come back in Horizontal scroll?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="score-view-horizontal",
        title="The score view in Horizontal scroll",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=4.0,
        extras=(
            Question(
                "one-long-line",
                "Is all the music on one long line, with a horizontal scrollbar and no second "
                "line below it?",
            ),
            Question(
                "scrolls-to-the-end",
                "Scroll right to the end: is the final barline there, with nothing wrapped onto "
                "another line?",
                behavioural=True,
            ),
        ),
    ),
    # The same score with more room. Worth its own picture because system
    # breaking is the one layout decision that cannot be seen at a single width:
    # a bar is either on this line or the next, and the only way to see that the
    # break is in a sensible place is to see it move.
    Surface(
        state="score-view-notation-large",
        title="The score view drawing a staff, with room to spare",
        area="score",
        modes=frozenset({FULL}),
        warmup_seconds=3.0,
        extras=(
            Question(
                "fewer-systems-than-docked",
                "Does the extra width put more bars on each line, rather than stretching the "
                "same bars wider?",
            ),
            Question(
                "systems-evenly-stacked",
                "Is the vertical gap between staves the same for every pair, with room for the "
                "lyrics in between?",
            ),
            Question(
                "tie-drawn-as-a-curve",
                "Is the tie between the two held notes a curve from one notehead to the other, "
                "bowing away from the stems?",
            ),
            Question(
                "chord-notes-share-a-stem",
                "Does the chord near the end share one stem, with the two notes a step apart "
                "offset sideways rather than printed on top of each other?",
            ),
        ),
    ),
    Surface(
        state="tuner-docked",
        title="Workspace layout after a restart",
        modes=frozenset({FULL}),
        restart_before=True,
        instruction=(
            "Before this surface the application was restarted. Compare what you "
            "see against the layout that was open before."
        ),
        extras=(
            Question(
                "layout-restored", "Was the workspace layout restored?", behavioural=True
            ),
        ),
    ),
)


def questions_for(surface: Surface) -> tuple[Question, ...]:
    """The three fixed axes, then this surface's own questions."""
    return FIXED_AXES + surface.extras


def attended_questions(surface: Surface) -> tuple[Question, ...]:
    """Questions a captured image cannot answer.

    A surface carrying an instruction has all of its extras here: the tester has
    to do something before any of them mean anything, and by then they are
    looking at a live application rather than a screenshot.
    """
    if surface.instruction:
        return surface.extras

    return tuple(question for question in surface.extras if question.behavioural)


def review_questions(surface: Surface) -> tuple[Question, ...]:
    """Questions answerable from the captured image: the axes plus appearance extras."""
    attended = {question.id for question in attended_questions(surface)}

    return FIXED_AXES + tuple(
        question for question in surface.extras if question.id not in attended
    )


def find(state: str, title: str) -> Surface | None:
    """The surface a stored capture came from.

    Matched on both state and title because one state serves several surfaces —
    `tuner-docked` is both "the tuner, docked" and "workspace layout after a
    restart", which ask different questions.
    """
    for surface in SURFACES:
        if surface.state == state and surface.title == title:
            return surface

    return None


def surfaces_needing_attendance(mode: str) -> tuple[Surface, ...]:
    """The subset the attended pass covers — deliberately much smaller than the run."""
    return tuple(
        surface for surface in surfaces_for_mode(mode) if attended_questions(surface)
    )


def surfaces_for_mode(mode: str) -> tuple[Surface, ...]:
    """Every surface a run in `mode` presents, in order."""
    if mode not in (QUICK, FULL):
        raise ValueError(f"Unknown mode '{mode}'; expected '{QUICK}' or '{FULL}'.")

    return tuple(surface for surface in SURFACES if mode in surface.modes)


def resolutions_for_surface(surface: Surface, resolutions: tuple[str, ...]) -> tuple[str, ...]:
    """Resolutions a particular surface is captured at.

    A surface that is about its own window size is captured once regardless of
    the configured set, because resizing it would destroy what is under test.
    """
    if surface.fixed_geometry or surface.window_title:
        return (DEFAULT_GEOMETRY,)

    return resolutions


# What one capture costs, near enough to weight a progress bar by.
#
# Counting surfaces makes the bar lie: a surface carrying a tone waits its
# warmup and then settles, and a plain one is photographed almost immediately.
# A run of the quick plan is half over by count long before it is half over by
# time, which is the opposite of what a bar is for.
#
# These are relative, not a promise about seconds. Whatever they get wrong about
# scale, the caller's own measured rate corrects; what they have to get right is
# which surfaces are the expensive ones.
CAPTURE_COST = 1.0
STATE_CHANGE_COST = 0.5
RESTART_COST = 3.0


def capture_cost(surface: "Surface", *, first_of_group: bool = False) -> float:
    """Roughly what one entry in a plan will take, relative to the others."""
    cost = CAPTURE_COST

    if first_of_group:
        # Opening the state and filling the tool's history, both of which the
        # rest of this surface's resolutions inherit rather than repeat.
        cost += STATE_CHANGE_COST + surface.warmup_seconds

        if surface.restart_before:
            cost += RESTART_COST

    return cost


def plan_cost(plan: tuple[tuple["Surface", str, str], ...]) -> float:
    """What a whole plan costs, in the same units."""
    total = 0.0
    key: tuple[str, str, str] | None = None

    for surface, _, theme in plan:
        here = (surface.state, surface.title, theme)
        total += capture_cost(surface, first_of_group=here != key)
        key = here

    return total


def plan(
    mode: str,
    resolutions: tuple[str, ...] = DEFAULT_RESOLUTIONS,
    themes: tuple[str, ...] = DEFAULT_THEMES,
    states: tuple[str, ...] = (),
) -> tuple[tuple[Surface, str, str], ...]:
    """The full ordered list of (surface, resolution, theme) a run captures.

    Themes are the outer loop: switching palette is one command and repainting
    is instant, while a resize has to settle, so grouping by theme keeps the
    number of resizes the same as it was before palettes existed.

    `states` narrows the run to particular surfaces by approved-state name. A
    full sweep is minutes of window resizing; someone who changed one tool wants
    that tool, and an unknown name is an error rather than an empty run, because
    a typo that silently captures nothing looks exactly like a surface that is
    fine.
    """
    if not resolutions:
        raise ValueError("A run must cover at least one resolution.")

    if not themes:
        raise ValueError("A run must cover at least one theme.")

    unknown = [theme for theme in themes if theme not in THEMES]

    if unknown:
        raise ValueError(f"Unknown theme(s): {', '.join(unknown)}")

    chosen = surfaces_for_mode(mode)

    if states:
        available = {surface.state for surface in chosen}
        missing = [state for state in states if state not in available]

        if missing:
            raise ValueError(
                f"Unknown surface state(s) for {mode} mode: {', '.join(sorted(missing))}. "
                f"Known: {', '.join(sorted(available))}"
            )

        wanted = set(states)
        chosen = tuple(surface for surface in chosen if surface.state in wanted)

    return tuple(
        (surface, geometry, theme)
        for theme in themes
        for surface in chosen
        for geometry in resolutions_for_surface(surface, resolutions)
    )


def validate() -> list[str]:
    """Problems with the surface list itself, empty when it is coherent.

    Run before a session starts, so a malformed list fails immediately rather
    than partway through a tester's run.
    """
    problems: list[str] = []

    quick = {surface.title for surface in surfaces_for_mode(QUICK)}
    full = {surface.title for surface in surfaces_for_mode(FULL)}

    # A quick run must be a strict subset of a full one, or "quick" stops
    # meaning "less than full" and the two become incomparable.
    for title in sorted(quick - full):
        problems.append(f"'{title}' is in quick mode but not in full mode.")

    manifest = approved_states.manifest()

    # The window titles code and tests look for by name, against the ones the
    # application declares.
    declared_titles = {state.window_title for state in manifest.states}

    for title in (SETTINGS_WINDOW, FEEDBACK_WINDOW, SCORE_IMPORT_WINDOW):
        if title not in declared_titles:
            problems.append(f"No approved state shows a window titled '{title}'.")

    for theme in THEMES:
        if theme not in manifest.themes:
            problems.append(f"The application has no theme named '{theme}'.")

    seen: set[tuple[str, str]] = set()

    for surface in SURFACES:
        # Caught here as well as against `list-states` at run time, so a
        # surface of a state that does not exist fails the unit tests rather
        # than a capture run.
        if surface.state and surface.state not in manifest.state_ids:
            problems.append(
                f"'{surface.title}' names state '{surface.state}', which the application "
                "does not approve."
            )
        else:
            for name, value in derived_fields(surface.state).items():
                if getattr(surface, name) != value:
                    problems.append(
                        f"'{surface.title}' says {name} is {getattr(surface, name)!r}; the "
                        f"application's state says {value!r}."
                    )

        if not surface.title:
            problems.append(f"Surface for state '{surface.state}' has no title.")

        if not surface.state:
            problems.append(f"Surface '{surface.title}' names no state.")

        if not surface.modes:
            problems.append(f"'{surface.title}' is in no run mode, so it is never asked.")

        key = (surface.state, surface.title)

        if key in seen:
            problems.append(f"Duplicate surface: state '{surface.state}', title '{surface.title}'.")

        seen.add(key)

        ids = [question.id for question in questions_for(surface)]

        if len(ids) != len(set(ids)):
            problems.append(f"'{surface.title}' asks the same question id twice.")

        for question in questions_for(surface):
            if not question.prompt.strip():
                problems.append(f"'{surface.title}' has a question with no prompt.")

    return problems


def required_states() -> frozenset[str]:
    """Every application state the surface list depends on.

    Checked against the application's own `list-states` before a run, so a
    harness that has drifted fails up front instead of halfway through.
    """
    return frozenset(surface.state for surface in SURFACES)
