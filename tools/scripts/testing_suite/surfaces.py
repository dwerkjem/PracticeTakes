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

# The three fixed axes, asked for every surface in this order.
FIXED_AXES: tuple[Question, ...]  # forward reference, defined below

QUICK = "quick"
FULL = "full"

# Titles the application gives its other top-level windows, from
# SettingsWindow.h and FeedbackWindow.h. A capture of one of those surfaces has
# to name the window or it photographs the main one instead.
SETTINGS_WINDOW = "Settings"
FEEDBACK_WINDOW = "Send feedback"

# The resolutions a run captures each surface at. Configuration rather than a
# fixed list: `--resolutions` overrides it, and the set a run covered is
# recorded with the run so two runs covering different sets are comparable on
# purpose rather than by accident.
DEFAULT_GEOMETRY = "default"
SWEEP_GEOMETRIES = (DEFAULT_GEOMETRY, "constrained", "maximised")
DEFAULT_RESOLUTIONS = SWEEP_GEOMETRIES


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

    # Something the tester must do by hand before answering, because it is an
    # interaction rather than a state. The harness prompts and waits; it does
    # not simulate it.
    instruction: str = ""

    # This surface *is about* its window size, so the geometry sweep must leave
    # it alone. Sweeping it would resize the window to each geometry in turn and
    # so destroy the very thing under test -- which is exactly what happened:
    # narrow-window was reported "not narrow" and fullscreen "not fullscreen",
    # because the sweep set them both back to the default size.
    fixed_geometry: bool = False

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
    window_title: str = ""


# The surface set. Ordered as a run presents them: cheapest and most
# fundamental first, so a broken build fails early rather than after a tester
# has answered twenty questions.
SURFACES: tuple[Surface, ...] = (
    Surface(
        state="empty",
        title="The shell with no tool open",
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
        modes=frozenset({QUICK, FULL}),
        window_title=SETTINGS_WINDOW,
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
        window_title="Tuner",
        extras=(
            Question(
                "moveable", "Can the floating window be moved and resized?", behavioural=True
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
        title="All three tools open at once",
        modes=frozenset({FULL}),
        extras=(
            Question(
                "all-live", "Do all three respond to sound at the same time?", behavioural=True
            ),
        ),
    ),
    Surface(
        state="narrow-window",
        title="A narrow window with the collapsed menu",
        modes=frozenset({FULL}),
        fixed_geometry=True,
        extras=(
            Question(
                "hamburger",
                "Does the collapsed menu open and list the same actions?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="fullscreen",
        title="Fullscreen",
        modes=frozenset({FULL}),
        fixed_geometry=True,
        extras=(
            Question("escape", "Does Escape or F11 leave fullscreen?", behavioural=True),
        ),
    ),
    Surface(
        state="microphone-muted",
        title="Global microphone mute engaged",
        modes=frozenset({FULL}),
        extras=(
            Question("mute-obvious", "Is it obvious at a glance that input is muted?"),
            Question(
                "mute-silences", "Do the tools stop responding to sound?", behavioural=True
            ),
        ),
    ),
    Surface(
        state="microphone-warning",
        title="The no-usable-input warning",
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
        modes=frozenset({FULL}),
        window_title=SETTINGS_WINDOW,
        instruction="Switch to a different input device, then back.",
        extras=(
            Question(
                "switching",
                "Did switching device take effect without a restart?",
                behavioural=True,
            ),
        ),
    ),
    Surface(
        state="settings-appearance",
        title="Settings: appearance and theme",
        modes=frozenset({FULL}),
        window_title=SETTINGS_WINDOW,
        instruction="Change the theme, and look at a tool with it applied.",
        extras=(Question(
                "theme-applies", "Did the theme apply everywhere?", behavioural=True
            ),),
    ),
    Surface(
        state="settings-open",
        title="Settings: import and export round trip",
        modes=frozenset({FULL}),
        window_title=SETTINGS_WINDOW,
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
        state="feedback-open",
        title="The feedback form",
        modes=frozenset({FULL}),
        window_title=FEEDBACK_WINDOW,
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


def plan(mode: str, resolutions: tuple[str, ...] = DEFAULT_RESOLUTIONS) -> tuple[tuple[Surface, str], ...]:
    """The full ordered list of (surface, resolution) pairs a run captures."""
    if not resolutions:
        raise ValueError("A run must cover at least one resolution.")

    return tuple(
        (surface, geometry)
        for surface in surfaces_for_mode(mode)
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

    seen: set[tuple[str, str]] = set()

    for surface in SURFACES:
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
