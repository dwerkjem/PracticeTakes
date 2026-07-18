from __future__ import annotations

from .models import Stage

OWNER = "dwerkjem"
REPO = "PracticeTakes"
REPO_NWO = f"{OWNER}/{REPO}"
PROJECT_TITLE = "Practice Takes Roadmap"
PROJECT_DESCRIPTION = (
    "Solo-developer roadmap for Practice Takes, covering the audio foundation, "
    "dockable tools, MusicXML and MIDI practice, live performance evaluation, "
    "and final product hardening."
)
ROOT_ISSUE = 10

STAGES = (
    Stage("First Draft", "Stage 1", "Month 1–2", 6),
    Stage("Functional Alpha", "Stage 2", "Month 3–4", 10),
    Stage("MVP", "Stage 3", "Month 5–8", 16),
    Stage("Performance Beta", "Stage 4", "Month 9–14", 24),
    Stage("Final Feature Release", "Stage 5", "Month 15–18", 16),
)

FIELD_OPTIONS = {
    "Stage": tuple(stage.name for stage in STAGES),
    "Priority": ("Critical", "High", "Normal", "Later"),
    "Estimate": ("XS", "S", "M", "L", "XL"),
    "Target window": tuple(stage.target_window for stage in STAGES),
}

VIEW_SPECS = (
    ("Development Board", "board", "is:issue"),
    ("Roadmap Timeline", "roadmap", "is:issue"),
    ("Full Backlog", "table", "is:issue"),
    (
        "Current Work",
        "table",
        'status:"Ready","In progress","Review and testing","Blocked"',
    ),
    ("Performance Mode", "table", 'stage:"Performance Beta"'),
    ("Tools and Workspace", "table", 'stage:"Functional Alpha"'),
)

CRITICAL_TITLE_PATTERNS = (
    "shared microphone selection",
    "real-time-safe fan-out",
    "dockable and floating tool-window foundation",
    "musicxml: import",
    "midi: import",
    "transport: midi playback",
    "latency calibration",
    "performance mode: session setup",
    "pitch matching",
    "onset timing",
    "missed, extra, and wrong-note",
    "clock-drift correction",
    "autosave, crash recovery",
    "cross-platform test matrix",
    "feedback service",
)

TRACKER_TITLE_PREFIXES = ("Roadmap:", "Stage ", "Feature:")
