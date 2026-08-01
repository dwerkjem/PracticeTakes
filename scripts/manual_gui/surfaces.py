#!/usr/bin/env python3
"""What the manual GUI harness asks about, as data.

A surface is one approved application state plus the questions to ask while
looking at it. Surfaces are declarative on purpose: adding one is an edit to
this list, not a change to the harness, and the list is the reviewable record of
what gets verified before a release.

Every surface is scored on the same three axes, so runs stay comparable — you
can see that presentation regressed on the tuner between two versions, which a
bespoke question set per surface could never tell you. Surfaces may add their
own questions, recorded separately so they do not dilute that comparable core.

Standard library only, so the harness's tests run without Textual installed.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# The three fixed axes, asked for every surface in this order.
FIXED_AXES: tuple[Question, ...]  # forward reference, defined below

QUICK = "quick"
FULL = "full"

# Window geometries the optional sweep repeats each surface at. "default" is
# what a surface gets when the sweep is off.
DEFAULT_GEOMETRY = "default"
SWEEP_GEOMETRIES = (DEFAULT_GEOMETRY, "constrained", "maximised")


@dataclass(frozen=True)
class Question:
    id: str
    prompt: str


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
            Question("live-input", "Does the tuner respond to sound from the microphone?"),
        ),
    ),
    Surface(
        state="settings-open",
        title="The settings window",
        modes=frozenset({QUICK, FULL}),
    ),
    Surface(
        state="spectrogram-docked",
        title="The spectrogram, docked",
        modes=frozenset({FULL}),
        extras=(
            Question("live-input", "Does the spectrogram respond to sound from the microphone?"),
        ),
    ),
    Surface(
        state="harmonics-docked",
        title="The harmonic analyser, docked",
        modes=frozenset({FULL}),
        extras=(
            Question("live-input", "Does the analyser respond to sound from the microphone?"),
        ),
    ),
    Surface(
        state="tuner-floating",
        title="The tuner in a floating window",
        modes=frozenset({FULL}),
        extras=(
            Question("moveable", "Can the floating window be moved and resized?"),
        ),
    ),
    Surface(
        state="two-tools-split",
        title="Two tools tiled side by side",
        modes=frozenset({FULL}),
        extras=(
            Question("divider", "Can the divider between the two be dragged?"),
        ),
    ),
    Surface(
        state="two-tools-tabbed",
        title="Two tools sharing a tab strip",
        modes=frozenset({FULL}),
        extras=(
            Question("switching", "Does switching tabs show the other tool?"),
        ),
    ),
    Surface(
        state="all-tools-docked",
        title="All three tools open at once",
        modes=frozenset({FULL}),
        extras=(
            Question("all-live", "Do all three respond to sound at the same time?"),
        ),
    ),
    Surface(
        state="narrow-window",
        title="A narrow window with the collapsed menu",
        modes=frozenset({FULL}),
        extras=(
            Question("hamburger", "Does the collapsed menu open and list the same actions?"),
        ),
    ),
    Surface(
        state="fullscreen",
        title="Fullscreen",
        modes=frozenset({FULL}),
        extras=(Question("escape", "Does Escape or F11 leave fullscreen?"),),
    ),
    Surface(
        state="microphone-muted",
        title="Global microphone mute engaged",
        modes=frozenset({FULL}),
        extras=(
            Question("mute-obvious", "Is it obvious at a glance that input is muted?"),
            Question("mute-silences", "Do the tools stop responding to sound?"),
        ),
    ),
    Surface(
        state="microphone-warning",
        title="The no-usable-input warning",
        modes=frozenset({FULL}),
        extras=(
            Question("actionable", "Does the warning say what to do about it?"),
            Question("dismissable", "Can it be dismissed?"),
        ),
    ),
    Surface(
        state="settings-audio-device",
        title="Settings: audio device selection",
        modes=frozenset({FULL}),
        instruction="Switch to a different input device, then back.",
        extras=(
            Question("switching", "Did switching device take effect without a restart?"),
        ),
    ),
    Surface(
        state="settings-appearance",
        title="Settings: appearance and theme",
        modes=frozenset({FULL}),
        instruction="Change the theme, and look at a tool with it applied.",
        extras=(Question("theme-applies", "Did the theme apply everywhere?"),),
    ),
    Surface(
        state="settings-open",
        title="Settings: import and export round trip",
        modes=frozenset({FULL}),
        instruction=(
            "Export settings to a file, change something, then import the file back."
        ),
        extras=(
            Question("round-trip", "Did importing restore exactly what was exported?"),
            Question("reports", "Did it say clearly whether the import succeeded?"),
        ),
    ),
    Surface(
        state="feedback-open",
        title="The feedback form",
        modes=frozenset({FULL}),
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
            Question("layout-restored", "Was the workspace layout restored?"),
        ),
    ),
)


def questions_for(surface: Surface) -> tuple[Question, ...]:
    """The three fixed axes, then this surface's own questions."""
    return FIXED_AXES + surface.extras


def surfaces_for_mode(mode: str) -> tuple[Surface, ...]:
    """Every surface a run in `mode` presents, in order."""
    if mode not in (QUICK, FULL):
        raise ValueError(f"Unknown mode '{mode}'; expected '{QUICK}' or '{FULL}'.")

    return tuple(surface for surface in SURFACES if mode in surface.modes)


def geometries_for(sweep: bool) -> tuple[str, ...]:
    """Geometries each surface is presented at.

    Off by default because it multiplies how many prompts a run asks, and most
    runs do not need it.
    """
    return SWEEP_GEOMETRIES if sweep else (DEFAULT_GEOMETRY,)


def plan(mode: str, sweep: bool) -> tuple[tuple[Surface, str], ...]:
    """The full ordered list of (surface, geometry) pairs a run will present."""
    return tuple(
        (surface, geometry)
        for surface in surfaces_for_mode(mode)
        for geometry in geometries_for(sweep)
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
