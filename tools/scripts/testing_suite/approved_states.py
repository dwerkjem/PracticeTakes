#!/usr/bin/env python3
"""The application's approved window states, read from the generated manifest.

The application decides which states it will enter on request, in
src/application/testcontrol/ApprovedWindowStates.cpp, and its build writes them
out to docs/contracts/testcontrol/approved-states.json. This reads that file, so
the harness knows what each state opens, how, and which window it is a picture
of without restating any of it. Before the manifest, `surfaces.py` wrote those
facts out a second time and nothing kept the two lists equal: it filed
`all-tools-docked` under four tools while the application opened five.

The file is committed, so the harness and its tests read it without a C++
build. A local build of PracticeTakesTests rewrites it whenever the list
changes; under CI, ApprovedStatesManifestTests fails when it is stale.

Standard library only.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import cache
import json
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
MANIFEST_PATH = REPOSITORY_ROOT / "docs" / "contracts" / "testcontrol" / "approved-states.json"

# The application's name for the size every state opens at unless it says
# otherwise. A state at any other geometry is about its window size.
NORMAL_GEOMETRY = "normal"


@dataclass(frozen=True)
class ApprovedState:
    """One state, as the application declares it."""

    id: str
    description: str

    # As the state names them, which may be a historical alias ("harmonics"),
    # and resolved to the tool catalog's canonical ids.
    tools: tuple[str, ...]
    tool_ids: tuple[str, ...]

    # "none", "docked" or "floating"; and "single", "split" or "tabbed".
    presentation: str
    arrangement: str

    geometry: str
    microphone: str
    settings_open: bool
    settings_panel: str
    feedback_open: bool
    tone_hz: float
    tool_view: str
    score_fixture: str
    show_score_import_summary: bool
    hold_score_import_in_progress: bool

    # Whether the practice clock is playing the score once it has imported.
    clock_playing: bool

    # The zero-based bar the clock is moved to once the score imports, or -1.
    clock_bar: int

    # Whether a take is recorded once the score imports.
    record_take: bool

    # Whether a synthetic keyboard take is kept and reviewed once the score
    # imports.
    review_take: bool

    # Whether playback is turned on, the second track muted, and the Mix popup
    # opened once the score imports.
    mix_open: bool

    # The reference pitch for the picture, in hertz; zero for A440.
    reference_pitch_hz: float

    # With review_take, the take is a synthetic singer's rather than a
    # pianist's.
    sung_take: bool

    # The text size the picture is taken at, as a percentage; 100 for most.
    text_scale_percent: int

    # The top-level window the state is a picture of. Empty for the main one.
    window_title: str

    @property
    def is_about_its_geometry(self) -> bool:
        return self.geometry != NORMAL_GEOMETRY


@dataclass(frozen=True)
class Manifest:
    themes: tuple[str, ...]
    geometries: tuple[str, ...]
    click_targets: tuple[str, ...]
    states: tuple[ApprovedState, ...]

    def find(self, state_id: str) -> ApprovedState | None:
        for state in self.states:
            if state.id == state_id:
                return state

        return None

    @property
    def state_ids(self) -> frozenset[str]:
        return frozenset(state.id for state in self.states)


def _state_from(entry: dict) -> ApprovedState:
    return ApprovedState(
        id=entry["id"],
        description=entry["description"],
        tools=tuple(entry["tools"]),
        tool_ids=tuple(entry["toolIds"]),
        presentation=entry["presentation"],
        arrangement=entry["arrangement"],
        geometry=entry["geometry"],
        microphone=entry["microphone"],
        settings_open=entry["settingsOpen"],
        settings_panel=entry["settingsPanel"],
        feedback_open=entry["feedbackOpen"],
        tone_hz=float(entry["toneHz"]),
        tool_view=entry["toolView"],
        score_fixture=entry["scoreFixture"],
        show_score_import_summary=entry["showScoreImportSummary"],
        hold_score_import_in_progress=entry["holdScoreImportInProgress"],
        clock_playing=entry["clockPlaying"],
        clock_bar=int(entry["clockBar"]),
        record_take=entry["recordTake"],
        review_take=entry["reviewTake"],
        mix_open=entry["mixOpen"],
        reference_pitch_hz=float(entry["referencePitchHz"]),
        sung_take=entry["sungTake"],
        text_scale_percent=int(entry["textScalePercent"]),
        window_title=entry["windowTitle"],
    )


def parse(text: str) -> Manifest:
    """A manifest from the file's text. A missing key is a KeyError, not a guess."""
    document = json.loads(text)

    return Manifest(
        themes=tuple(document["themes"]),
        geometries=tuple(document["geometries"]),
        click_targets=tuple(target["id"] for target in document["clickTargets"]),
        states=tuple(_state_from(entry) for entry in document["states"]),
    )


@cache
def manifest() -> Manifest:
    """The committed manifest, read once."""
    return parse(MANIFEST_PATH.read_text(encoding="utf-8"))
