#!/usr/bin/env python3
"""Running suites — and building what they need — in the background.

This is what the hub's buttons drive. The point of the hub is that "run the
tests" should not mean remembering five commands, three build directories, and
which of them needs a display; so the job owns the whole chain and reports what
it is doing while it does it.

For each suite it: builds whatever the suite needs that is missing, runs it,
reads counts out of the output, and records the result against the run. A
non-zero exit is a failure whatever the output said — counts are best-effort,
verdicts are not.

Two things are deliberate:

**Builds run with a sanitised environment** — `/usr/bin:/bin` and nothing else on
the PATH. On a machine with Nix alongside the distribution's packages, inheriting
the ambient PATH puts Nix's loader in front of the system one and `juceaide` dies
with "libexpat.so.1 not found" partway through. That is a miserable failure to
hit from a button, so it is prevented rather than diagnosed.

**A run is one build under test**, holding everything anybody learned about it:
captures, verdicts, suite results, measurements. That is what makes "what state
was this build in" one question rather than five.

Standard library only.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import datetime as _datetime
import os
from pathlib import Path
import platform as _platform
import re
import subprocess
import threading
import time

import capture as capture_module
import machine as machine_module
import suites as suites_module
import surfaces
from driver import ApplicationDriver, missing_states
from store import Store

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
CONTROL_BUILD = REPOSITORY_ROOT / "build-tc"
TEST_BUILD = REPOSITORY_ROOT / "build"
DEFAULT_EXECUTABLE = CONTROL_BUILD / "bin" / "PracticeTakes"
SOURCE_ROOT = REPOSITORY_ROOT / "src"

BUILD_PATH = "/usr/bin:/bin"
LOG_LINES_KEPT = 200

IDLE = "idle"
BUILDING = "building"
RUNNING = "running"
FINISHED = "finished"
FAILED = "failed"

# How each build target is produced: where it goes, and what has to be switched
# on for it to exist at all.
BUILD_TARGETS = {
    "PracticeTakes": {
        "directory": CONTROL_BUILD,
        "binary": CONTROL_BUILD / "bin" / "PracticeTakes",
        "options": ("-DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON",),
        "why": "the application, with the control channel the suite drives it through",
    },
    "PracticeTakesTests": {
        "directory": TEST_BUILD,
        "binary": TEST_BUILD / "bin" / "PracticeTakesTests",
        "options": ("-DBUILD_TESTING=ON",),
        "why": "the C++ unit and benchmark binary",
    },
}


def _now() -> str:
    return _datetime.datetime.now().replace(microsecond=0).isoformat()


def build_environment() -> dict[str, str]:
    environment = {"PATH": BUILD_PATH}

    for name in ("HOME", "USER", "LOGNAME", "DISPLAY", "XAUTHORITY", "LANG", "TERM"):
        value = os.environ.get(name)

        if value:
            environment[name] = value

    return environment


def cmake_binary() -> str:
    """The distribution's cmake in preference to whatever is first on PATH."""
    return "/usr/bin/cmake" if Path("/usr/bin/cmake").exists() else "cmake"


# Walking src/ takes long enough to notice, and the hub asks whether a build is
# stale on every poll. A few seconds of staleness in the answer costs nothing;
# a full directory walk per second does.
_SOURCE_SCAN_TTL_SECONDS = 5.0
_source_scan: tuple[float, float] = (0.0, 0.0)


def newest_source_change() -> float:
    global _source_scan

    now = time.monotonic()
    scanned_at, value = _source_scan

    if value and now - scanned_at < _SOURCE_SCAN_TTL_SECONDS:
        return value

    latest = (REPOSITORY_ROOT / "CMakeLists.txt").stat().st_mtime

    for path in SOURCE_ROOT.rglob("*"):
        if path.is_file() and path.suffix in (".cpp", ".h"):
            latest = max(latest, path.stat().st_mtime)

    _source_scan = (now, latest)

    return latest


def build_state(target: str = "PracticeTakes") -> dict:
    """Whether a target is there and whether it predates the sources.

    Reported to the page so a button can say "build and run" rather than
    appearing to hang for ten minutes on a machine that had no build.
    """
    entry = BUILD_TARGETS.get(target)

    if entry is None:
        return {"target": target, "present": True, "stale": False, "reason": ""}

    binary = entry["binary"]

    if not binary.is_file():
        return {"target": target, "present": False, "stale": False,
                "reason": f"not built yet — {entry['why']}"}

    stale = binary.stat().st_mtime < newest_source_change()

    return {
        "target": target,
        "present": True,
        "stale": stale,
        "reason": "sources have changed since this was built" if stale else "",
    }


def build_overview() -> list[dict]:
    return [build_state(target) for target in BUILD_TARGETS]


def display_available() -> bool:
    return bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))


@dataclass
class Job:
    """One background run of one or more suites."""

    store: Store
    state: str = IDLE
    message: str = ""
    run_id: int | None = None
    percent: int = 0
    started_at: str = ""
    finished_at: str = ""
    queued: list[str] = field(default_factory=list)
    results: dict = field(default_factory=dict)
    log: list[str] = field(default_factory=list)
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)
    _thread: threading.Thread | None = field(default=None, repr=False)

    # --- Status -------------------------------------------------------------

    @property
    def running(self) -> bool:
        return self.state in (BUILDING, RUNNING)

    def status(self) -> dict:
        with self._lock:
            return {
                "state": self.state,
                "message": self.message,
                "run_id": self.run_id,
                "percent": self.percent,
                "started_at": self.started_at,
                "finished_at": self.finished_at,
                "queued": list(self.queued),
                "results": dict(self.results),
                "log": list(self.log[-LOG_LINES_KEPT:]),
                "running": self.running,
            }

    def _say(self, line: str, *, state: str = "", percent: int | None = None) -> None:
        with self._lock:
            if state:
                self.state = state

            if percent is not None:
                self.percent = max(0, min(100, percent))

            self.message = line
            self.log.append(line)
            del self.log[:-LOG_LINES_KEPT]

    def _record(self, suite_id: str, **fields: object) -> None:
        with self._lock:
            entry = dict(self.results.get(suite_id, {"id": suite_id}))
            entry.update(fields)
            self.results[suite_id] = entry

    # --- Starting -----------------------------------------------------------

    def start(
        self,
        suite_ids: list[str],
        *,
        mode: str = surfaces.FULL,
        resolutions: tuple[str, ...] = surfaces.DEFAULT_RESOLUTIONS,
        rebuild: bool = False,
        run_id: int | None = None,
    ) -> bool:
        """Kick off a job. False when one is already running."""
        chosen = [suite_id for suite_id in suite_ids if suites_module.by_id(suite_id)]

        if not chosen:
            return False

        with self._lock:
            if self.running:
                return False

            self.state = BUILDING
            self.message = "starting"
            self.percent = 0
            self.run_id = run_id
            self.started_at = _now()
            self.finished_at = ""
            self.queued = chosen
            self.results = {
                suite_id: {"id": suite_id, "state": "queued"} for suite_id in chosen
            }
            self.log = []

        self._thread = threading.Thread(
            target=self._work,
            kwargs={"chosen": chosen, "mode": mode, "resolutions": tuple(resolutions),
                    "rebuild": rebuild},
            daemon=True,
        )
        self._thread.start()

        return True

    def wait(self, timeout: float | None = None) -> None:
        """For the command line and for tests; the page polls instead."""
        if self._thread is not None:
            self._thread.join(timeout)

    # --- The work -----------------------------------------------------------

    def _work(self, *, chosen: list[str], mode: str, resolutions: tuple[str, ...], rebuild: bool) -> None:
        try:
            run_id = self.run_id or self.store.start_run(
                provenance=machine_module.provenance(),
                commit=_commit(),
                mode=mode,
                resolutions=resolutions,
                platform=f"{_platform.system()} {_platform.release()} ({_platform.machine()})",
            )

            with self._lock:
                self.run_id = run_id

            needed: list[str] = []

            for suite_id in chosen:
                suite = suites_module.by_id(suite_id)

                for target in suite.needs if suite else ():
                    if target not in needed and (rebuild or not build_state(target)["present"]):
                        needed.append(target)

            for index, target in enumerate(needed):
                floor = int(40 * index / len(needed))
                self._build(target, floor=floor, ceiling=int(40 * (index + 1) / len(needed)))

            for index, suite_id in enumerate(chosen):
                base = 40 + int(60 * index / len(chosen))
                self._run_suite(
                    suites_module.by_id(suite_id),
                    run_id=run_id,
                    mode=mode,
                    resolutions=resolutions,
                    floor=base,
                    ceiling=40 + int(60 * (index + 1) / len(chosen)),
                )

            failed = [
                entry for entry in self.results.values() if entry.get("state") == "failed"
            ]
            summary = f"{len(chosen) - len(failed)} of {len(chosen)} suite(s) passed"
            self._say(summary, state=FINISHED if not failed else FAILED, percent=100)
        except Exception as error:  # noqa: BLE001 - a background job reports rather than crashes
            self._say(f"stopped: {error}", state=FAILED)
        finally:
            with self._lock:
                self.finished_at = _now()

    # --- Building -----------------------------------------------------------

    def _build(self, target: str, *, floor: int, ceiling: int) -> None:
        entry = BUILD_TARGETS[target]
        self._say(f"building {target} — {entry['why']}", state=BUILDING, percent=floor)

        self._command(
            [
                cmake_binary(),
                "-S", str(REPOSITORY_ROOT),
                "-B", str(entry["directory"]),
                "-DCMAKE_BUILD_TYPE=Debug",
                "-DCMAKE_C_COMPILER=/usr/bin/gcc",
                "-DCMAKE_CXX_COMPILER=/usr/bin/g++",
                *entry["options"],
            ],
            label=f"configuring {target}",
            floor=floor,
            ceiling=floor + max(1, (ceiling - floor) // 10),
        )
        self._command(
            [cmake_binary(), "--build", str(entry["directory"]), "--target", target, "--parallel"],
            label=f"compiling {target}",
            floor=floor + max(1, (ceiling - floor) // 10),
            ceiling=ceiling,
        )

        if not Path(entry["binary"]).is_file():
            raise RuntimeError(f"the build finished but {entry['binary']} is not there")

    def _command(
        self,
        arguments: list[str],
        *,
        label: str,
        floor: int,
        ceiling: int,
        working_directory: Path | None = None,
        capture: list[str] | None = None,
    ) -> int:
        """Run a command, streaming its output into the log. Returns the exit code."""
        self._say(f"{label}…", percent=floor)

        try:
            process = subprocess.Popen(
                arguments,
                cwd=working_directory or REPOSITORY_ROOT,
                env=build_environment(),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except (OSError, ValueError) as error:
            self._say(f"{label} could not start: {error}")

            return 127

        assert process.stdout is not None

        for line in process.stdout:
            line = line.rstrip()

            if not line:
                continue

            if capture is not None:
                capture.append(line)

            # cmake prints "[ 42%] Building ..."; map that onto this step's band
            # so the page shows real progress rather than a spinner.
            match = re.match(r"\[\s*(\d+)%\]", line)
            percent = None

            if match:
                percent = int(floor + (ceiling - floor) * int(match.group(1)) / 100)

            self._say(line, percent=percent)

        return process.wait()

    # --- Suites -------------------------------------------------------------

    def _run_suite(
        self,
        suite: suites_module.Suite | None,
        *,
        run_id: int,
        mode: str,
        resolutions: tuple[str, ...],
        floor: int,
        ceiling: int,
    ) -> None:
        if suite is None:
            return

        if suite.needs_display and not display_available():
            self._record(suite.id, state="skipped", message="no display available")
            self._say(f"{suite.label}: skipped, no display", percent=ceiling)

            return

        self._record(suite.id, state="running")
        self._say(f"running {suite.label}", state=RUNNING, percent=floor)
        started = _datetime.datetime.now()

        if suite.id == "ui-capture":
            self._capture(run_id=run_id, mode=mode, resolutions=resolutions,
                          floor=floor, ceiling=ceiling)

            return

        output: list[str] = []
        code = self._command(
            list(suite.command),
            label=suite.label,
            floor=floor,
            ceiling=ceiling,
            working_directory=REPOSITORY_ROOT / suite.working_directory,
            capture=output,
        )
        seconds = (_datetime.datetime.now() - started).total_seconds()
        parsed = suite.parser("\n".join(output)) if suite.parser else {}

        # The exit code is the verdict. Counts are best effort: a suite whose
        # output could not be parsed still reports pass or fail correctly.
        failures = int(parsed.get("failures", 0)) or (0 if code == 0 else 1)
        cases = int(parsed.get("cases", 0))

        self.store.record_test_result(
            run_id,
            suite=suite.label,
            cases=cases,
            failures=failures,
            duration_seconds=float(parsed.get("duration_seconds") or seconds),
            summary=" ".join(suite.command),
        )

        measurements = parsed.get("measurements") or []

        if measurements:
            self.store.record_measurements(run_id, measurements, suite.id)

        self._record(
            suite.id,
            state="passed" if code == 0 else "failed",
            cases=cases,
            failures=failures,
            seconds=round(seconds, 1),
            measurements=len(measurements),
            message="" if code == 0 else f"exited {code}",
        )
        self._say(
            f"{suite.label}: {'passed' if code == 0 else 'FAILED'} "
            f"({cases} case(s), {failures} failure(s))",
            percent=ceiling,
        )

    def _capture(
        self,
        *,
        run_id: int,
        mode: str,
        resolutions: tuple[str, ...],
        floor: int,
        ceiling: int,
    ) -> None:
        problems = surfaces.validate()

        if problems:
            raise RuntimeError("the surface list is malformed: " + "; ".join(problems))

        plan = surfaces.plan(mode, resolutions)
        self._say("building the capture utilities", percent=floor)
        tooling = capture_module.Tooling.ensure()

        self._say("launching the application", percent=floor + 1)
        driver = ApplicationDriver(BUILD_TARGETS["PracticeTakes"]["binary"])
        driver.start()

        try:
            absent = missing_states(driver.list_states(), surfaces.required_states())

            if absent:
                raise RuntimeError("the application does not offer: " + ", ".join(absent))

            def report(entry: dict) -> None:
                fraction = entry["done"] / max(1, entry["total"])
                self._say(
                    f"capturing {entry['surface']} at {entry['geometry']} "
                    f"({entry['done'] + 1} of {entry['total']})",
                    percent=int(floor + (ceiling - floor) * fraction),
                )

            result = capture_module.CapturePass(
                store=self.store,
                run_id=run_id,
                driver=driver,
                tooling=tooling,
                image_directory=self.store.path.parent / "images" / f"run-{run_id}",
            ).run(plan, progress=report)
        finally:
            driver.stop()

        self._record(
            suite_id="ui-capture",
            state="failed" if result["failed"] else "passed",
            cases=result["captured"] + result["failed"],
            failures=result["failed"],
            message=f"{result['captured']} captured",
        )
        self._say(
            f"UI capture: {result['captured']} captured, {result['failed']} failed",
            percent=ceiling,
        )


def _commit() -> str:
    try:
        return subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=REPOSITORY_ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return "unknown"
