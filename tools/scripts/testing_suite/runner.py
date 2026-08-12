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

**Suites run with the developer's own environment.** The sanitised PATH is right
for compiling and wrong for everything else: `npm`, `node`, and `zsh` commonly
live in a Nix profile or a version manager, and running the service tests with
`/usr/bin:/bin` reported "npm: no such file" as a *test failure*. A tool that is
not installed is not a failing test — it is a suite that could not run, and it
says so.

**A run is one build under test**, holding everything anybody learned about it:
captures, verdicts, suite results, measurements. That is what makes "what state
was this build in" one question rather than five.

Standard library only.
"""

from __future__ import annotations

from contextlib import ExitStack

from dataclasses import dataclass, field
import datetime as _datetime
import os
from pathlib import Path
import platform as _platform
import re
import shutil
import signal
import subprocess
import threading
import time

import capture as capture_module
import display as display_module
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

# How long a command gets to end politely before it is killed.
STOP_GRACE_SECONDS = 5.0

# How much of a plan's weight has to be finished before a time estimate is worth
# showing. One surface is not a rate: the first is unrepresentative -- it pays
# for the application's first paint -- and an estimate that swings from four
# minutes to forty is worse than none.
ESTIMATE_AFTER_COST = 4.0

# What `_command` reports for a command that was never started, or was ended by
# a stop. Distinct from any exit code a real command produces, so "we stopped
# this" is never mistaken for "this failed".
STOPPED_CODE = -1000

IDLE = "idle"
BUILDING = "building"
RUNNING = "running"
FINISHED = "finished"
FAILED = "failed"

# How each build target is produced: where it goes, and what has to be switched
# on for it to exist at all.
#
# Deliberately no single hardcoded output path. `PracticeTakes` has an explicit
# RUNTIME_OUTPUT_DIRECTORY of `bin/`, while `PracticeTakesTests` has none and
# links into the build root -- so assuming either layout for both is wrong, and
# was: a build that succeeded was reported as "the build finished but the binary
# is not there". The target is looked for instead.
BUILD_TARGETS = {
    "PracticeTakes": {
        "directory": CONTROL_BUILD,
        "options": ("-DPRACTICE_TAKES_ENABLE_TEST_CONTROL=ON",),
        "why": "the application, with the control channel the suite drives it through",
    },
    "PracticeTakesTests": {
        "directory": TEST_BUILD,
        "options": ("-DBUILD_TESTING=ON",),
        "why": "the C++ unit and benchmark binary",
    },
}

# Where a generator might have put it, shallowest first.
BINARY_LOCATIONS = ("bin/{target}", "{target}", "*/{target}")


def binary_path(target: str) -> Path | None:
    """Where a built target actually is, or None when it has not been built.

    Searched rather than assumed: CMake puts these in different places depending
    on whether the target sets an output directory, and a wrong guess turns a
    successful build into a confusing failure.
    """
    entry = BUILD_TARGETS.get(target)

    if entry is None:
        return None

    directory = entry["directory"]

    for pattern in BINARY_LOCATIONS:
        for candidate in sorted(directory.glob(pattern.format(target=target))):
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate

    return None


def _now() -> str:
    return _datetime.datetime.now().replace(microsecond=0).isoformat()


def suite_environment() -> dict[str, str]:
    """The developer's own environment, because that is where their tools are."""
    return dict(os.environ)


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

    binary = binary_path(target)

    if binary is None:
        return {"target": target, "present": False, "stale": False,
                "reason": f"not built yet — {entry['why']}"}

    stale = binary.stat().st_mtime < newest_source_change()

    return {
        "target": target,
        "present": True,
        "stale": stale,
        "path": str(binary),
        "reason": "sources have changed since this was built" if stale else "",
    }


def remaining_seconds(entry: dict, elapsed: float) -> float | None:
    """How much longer this pass has, or None while that is still a guess.

    The plan's own weights say which surfaces are the expensive ones; this run's
    measured rate says what one of those weights is worth on this machine
    today. Neither alone is enough -- a static estimate is wrong on a slow
    machine, and a rate with nothing to scale is wrong whenever what is left
    does not resemble what is done.

    None until enough has finished to divide by, because a number that swings
    from four minutes to forty in its first seconds is worse than no number.
    """
    done = float(entry.get("cost_done", 0.0))
    total = float(entry.get("cost_total", 0.0))

    if total <= 0.0 or done < ESTIMATE_AFTER_COST or elapsed <= 0.0:
        return None

    return max(0.0, (total - done) * (elapsed / done))


def describe_remaining(seconds: float) -> str:
    """A duration to read at a glance, rounded to what it can honestly claim."""
    if seconds < 45:
        return "under a minute left"

    minutes = int(seconds / 60 + 0.5)

    return f"about {minutes} minute{'' if minutes == 1 else 's'} left"


def build_overview() -> list[dict]:
    return [build_state(target) for target in BUILD_TARGETS]


def display_available() -> bool:
    return bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))


def missing_program(command: str) -> str:
    """Why this command cannot be run at all, or "" when it can be.

    Checked before running rather than after failing, so "npm is not installed"
    reads as a suite that could not run rather than as a test that failed.
    """
    if not command or command.startswith("{"):
        return ""

    if "/" in command:
        path = REPOSITORY_ROOT / command

        if path.exists() and os.access(path, os.X_OK):
            return ""

        return f"`{command}` is not there, or is not executable"

    if shutil.which(command, path=suite_environment().get("PATH", "")) is None:
        return f"`{command}` is not installed, or is not on your PATH"

    return ""


class RunStopped(Exception):
    """Raised to unwind a job that was asked to stop.

    An exception rather than a return value checked at each step: the stop can
    happen inside a build, inside a suite, or between them, and every one of
    those paths has to end in the same place. A code that has to be checked is
    a code somebody forgets to check, and forgetting here means a stopped run
    quietly carrying on to the next suite.
    """


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

    # Raised by `stop`, read by the capture pass between surfaces. An Event
    # rather than a bool because it is set from the request thread and read from
    # the run thread, and because "is it set" is the whole interface.
    _stop: threading.Event = field(default_factory=threading.Event, repr=False)

    # --- Status -------------------------------------------------------------

    @property
    def running(self) -> bool:
        return self.state in (BUILDING, RUNNING)

    def stop(self) -> bool:
        """Ask a run in progress to stop. False when there is nothing to stop.

        The run ends at the next surface boundary rather than here: a capture
        interrupted between photographing and writing its row leaves a row with
        no file behind it.
        """
        with self._lock:
            if not self.running:
                return False

        self._stop.set()

        return True

    def stopping(self) -> bool:
        return self._stop.is_set()

    def status(self) -> dict:
        with self._lock:
            return {
                "stopping": self._stop.is_set(),
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
        themes: tuple[str, ...] = surfaces.DEFAULT_THEMES,
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

            # A previous run's stop must not end this one before it begins.
            self._stop.clear()
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
                    "themes": tuple(themes), "rebuild": rebuild},
            daemon=True,
        )
        self._thread.start()

        return True

    def start_build(self, targets: list[str] | None = None) -> bool:
        """Build, and do nothing else. False when something is already running.

        A build on its own, rather than one that happens on the way to a suite:
        the build is the slow part, it is what a source change invalidates, and
        wanting it done now without also running two hundred captures is the
        normal state of someone who has just edited the application.

        Deliberately no run row. A run is a record of what was verified, and a
        build verified nothing -- a row with no captures and no results under it
        would show up in the history as a run that did nothing.
        """
        chosen = [target for target in (targets or list(BUILD_TARGETS)) if target in BUILD_TARGETS]

        if not chosen:
            return False

        with self._lock:
            if self.running:
                return False

            self._stop.clear()
            self.state = BUILDING
            self.message = "starting"
            self.percent = 0
            self.run_id = None
            self.started_at = _now()
            self.finished_at = ""
            self.queued = list(chosen)
            self.results = {}
            self.log = []

        self._thread = threading.Thread(
            target=self._build_only, kwargs={"chosen": chosen}, daemon=True
        )
        self._thread.start()

        return True

    def _build_only(self, *, chosen: list[str]) -> None:
        done: list[str] = []

        try:
            for index, target in enumerate(chosen):
                if self.stopping():
                    raise RunStopped

                self._build(
                    target,
                    floor=int(100 * index / len(chosen)),
                    ceiling=int(100 * (index + 1) / len(chosen)),
                )
                done.append(target)

            self._say(f"built {', '.join(chosen)}", state=FINISHED, percent=100)
        except RunStopped:
            built = f", built {', '.join(done)} first" if done else ""
            self._say(f"stopped{built}", state=FINISHED, percent=100)
        except Exception as error:  # noqa: BLE001 - a background job reports rather than crashes
            self._say(f"build failed: {error}", state=FAILED)
        finally:
            with self._lock:
                self.finished_at = _now()

    def wait(self, timeout: float | None = None) -> None:
        """For the command line and for tests; the page polls instead."""
        if self._thread is not None:
            self._thread.join(timeout)

    # --- The work -----------------------------------------------------------

    def _work(self, *, chosen: list[str], mode: str, resolutions: tuple[str, ...],
              themes: tuple[str, ...], rebuild: bool) -> None:
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
                if self.stopping():
                    raise RunStopped

                floor = int(40 * index / len(needed))
                self._build(target, floor=floor, ceiling=int(40 * (index + 1) / len(needed)))

            for index, suite_id in enumerate(chosen):
                if self.stopping():
                    raise RunStopped

                base = 40 + int(60 * index / len(chosen))
                self._run_suite(
                    suites_module.by_id(suite_id),
                    run_id=run_id,
                    mode=mode,
                    resolutions=resolutions,
                    themes=themes,
                    floor=base,
                    ceiling=40 + int(60 * (index + 1) / len(chosen)),
                )

            failed = [
                entry for entry in self.results.values() if entry.get("state") == "failed"
            ]
            ran = [
                entry for entry in self.results.values()
                if entry.get("state") not in ("skipped", "unavailable", "stopped")
            ]
            summary = f"{len(ran) - len(failed)} of {len(ran)} suite(s) passed"

            if len(ran) < len(chosen):
                summary += f", {len(chosen) - len(ran)} could not run"
            self._say(summary, state=FINISHED if not failed else FAILED, percent=100)
        except RunStopped:
            # Everything that never got its turn, said out loud. A suite left
            # sitting at "queued" reads as still to come, and nothing is coming.
            for suite_id, entry in list(self.results.items()):
                if entry.get("state") in ("queued", "running"):
                    self._record(suite_id, state="stopped", message="the run was stopped")

            done = [
                entry for entry in self.results.values()
                if entry.get("state") in ("passed", "failed")
            ]
            # Finished, not failed: stopping is a decision somebody made, and a
            # run marked failed for it either blocks a release or teaches
            # everyone that failures can be ignored.
            self._say(
                f"stopped — {len(done)} of {len(chosen)} suite(s) ran",
                state=FINISHED,
                percent=100,
            )
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

        # A stopped build leaves no binary, which is not the same thing as a
        # build that finished and produced nothing -- and only one of those is
        # worth a message about where it looked.
        if self.stopping():
            raise RunStopped

        if binary_path(target) is None:
            looked = ", ".join(
                str(entry["directory"] / pattern.format(target=target))
                for pattern in BINARY_LOCATIONS
            )
            raise RuntimeError(f"the build finished but {target} is not in any of: {looked}")

    def _end(self, process: subprocess.Popen, label: str) -> None:
        """Signal a running command and everything it started.

        The process group, not the process. A build is cmake, which is make or
        ninja, which is however many compilers -- signalling only the one this
        job can see leaves the machine compiling for another minute while the
        page says the run has stopped.

        `terminate` first so a suite can tidy up; `kill` after a grace, because
        a stop that waits indefinitely for a well-behaved exit is the thing
        being fixed.
        """
        try:
            group = os.getpgid(process.pid)
        except OSError:  # already gone
            return

        self._say(f"stopping {label}…")

        try:
            os.killpg(group, signal.SIGTERM)
            process.wait(STOP_GRACE_SECONDS)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(group, signal.SIGKILL)
            except OSError:
                pass
        except OSError:
            pass

    def _watch_for_stop(self, process: subprocess.Popen, label: str) -> None:
        """End the command if the job is asked to stop while it runs.

        A thread rather than a check in the read loop: a suite that is thinking
        rather than printing -- a long test case, a link step -- produces no
        line to check on, and that is exactly when somebody presses stop.
        """
        while process.poll() is None:
            if self._stop.wait(0.25):
                self._end(process, label)

                return

    def _command(
        self,
        arguments: list[str],
        *,
        label: str,
        floor: int,
        ceiling: int,
        working_directory: Path | None = None,
        capture: list[str] | None = None,
        sanitised: bool = True,
    ) -> int:
        """Run a command, streaming its output into the log. Returns the exit code."""
        self._say(f"{label}…", percent=floor)

        if self.stopping():
            return STOPPED_CODE

        try:
            process = subprocess.Popen(
                arguments,
                cwd=working_directory or REPOSITORY_ROOT,
                env=build_environment() if sanitised else suite_environment(),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                # Its own process group, so a stop reaches the whole tree of
                # things it starts rather than just the top of it.
                start_new_session=True,
            )
        except (OSError, ValueError) as error:
            self._say(f"{label} could not start: {error}")

            return 127

        assert process.stdout is not None

        # `with` so the pipe is closed however this ends. A run is dozens of
        # commands and the hub is long-lived, so leaking one file each is a
        # descriptor limit somebody eventually hits.
        with process:
            threading.Thread(
                target=self._watch_for_stop, args=(process, label), daemon=True
            ).start()

            for line in process.stdout:
                line = line.rstrip()

                if not line:
                    continue

                if capture is not None:
                    capture.append(line)

                # cmake prints "[ 42%] Building ..."; map that onto this step's
                # band so the page shows real progress rather than a spinner.
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
        themes: tuple[str, ...],
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
            self._capture(run_id=run_id, mode=mode, resolutions=resolutions, themes=themes,
                          floor=floor, ceiling=ceiling)

            return

        unavailable = missing_program(suite.command[0] if suite.command else "")

        if unavailable:
            # Not a failing test: a tool that is not installed means the suite
            # never ran, and saying "failed" about it would be a lie that costs
            # somebody an afternoon.
            self._record(suite.id, state="unavailable", message=unavailable)
            self._say(f"{suite.label}: skipped — {unavailable}", percent=ceiling)

            return

        if suite.prepare and not (REPOSITORY_ROOT / suite.prepare_when_missing).exists():
            self._say(
                f"{suite.label}: {suite.prepare_when_missing} is missing — "
                f"running `{' '.join(suite.prepare)}` first",
                percent=floor,
            )
            prepared = self._command(
                list(suite.prepare),
                label=" ".join(suite.prepare),
                floor=floor,
                ceiling=floor,
                working_directory=REPOSITORY_ROOT / suite.working_directory,
                sanitised=False,
            )

            if self.stopping():
                self._record(suite.id, state="stopped", message="stopped part way")

                raise RunStopped

            if prepared != 0:
                self._record(
                    suite.id,
                    state="unavailable",
                    message=f"`{' '.join(suite.prepare)}` failed, so the suite could not run",
                )
                self._say(f"{suite.label}: skipped — its dependencies would not install",
                          percent=ceiling)

                return

        output: list[str] = []
        code = self._command(
            self._resolve(suite.command),
            label=suite.label,
            floor=floor,
            ceiling=ceiling,
            working_directory=REPOSITORY_ROOT / suite.working_directory,
            capture=output,
            sanitised=False,
        )

        # Before anything is written down. A killed process exits non-zero, and
        # recording that as a test result would put failures in the store, and
        # in the export the release gate reads, for tests that never finished.
        if self.stopping():
            self._record(suite.id, state="stopped", message="stopped part way")
            self._say(f"{suite.label}: stopped", percent=ceiling)

            raise RunStopped

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

        # A failure with no output at all is the confusing one: something is
        # wrong and the log says nothing about what. Say so rather than leaving
        # an empty panel to interpret.
        detail = f"exited {code}"

        if code != 0 and not output:
            detail = f"exited {code} and printed nothing — is `{suite.command[0]}` working?"

        self._record(
            suite.id,
            state="passed" if code == 0 else "failed",
            cases=cases,
            failures=failures,
            seconds=round(seconds, 1),
            measurements=len(measurements),
            message="" if code == 0 else detail,
        )
        self._say(
            f"{suite.label}: {'passed' if code == 0 else 'FAILED — ' + detail} "
            f"({cases} case(s), {failures} failure(s))",
            percent=ceiling,
        )

    def _resolve(self, command: tuple[str, ...]) -> list[str]:
        """Substitute {Target} in a command with wherever that target was built."""
        resolved: list[str] = []

        for argument in command:
            match = re.fullmatch(r"\{(\w+)\}", argument)

            if match:
                found = binary_path(match.group(1))

                if found is None:
                    raise RuntimeError(f"{match.group(1)} is not built")

                resolved.append(str(found))
            else:
                resolved.append(argument)

        return resolved

    def _capture(
        self,
        *,
        run_id: int,
        mode: str,
        resolutions: tuple[str, ...],
        themes: tuple[str, ...],
        floor: int,
        ceiling: int,
    ) -> None:
        problems = surfaces.validate()

        if problems:
            raise RuntimeError("the surface list is malformed: " + "; ".join(problems))

        plan = surfaces.plan(mode, resolutions, themes)
        self._say("building the capture utilities", percent=floor)
        tooling = capture_module.Tooling.ensure()

        self._say("launching the application", percent=floor + 1)
        executable = binary_path("PracticeTakes")

        if executable is None:
            raise RuntimeError("PracticeTakes is not built")

        # On a screen of its own, like the command line does. Without this the
        # hub's "run everything" opened the application on the operator's
        # desktop: windows over whatever they were doing, the pointer taken, and
        # nothing else clickable for the length of the run. The button most
        # likely to be pressed was the one that behaved worst.
        with ExitStack() as stack:
            try:
                screen = stack.enter_context(display_module.virtual_display())
                self._say(f"capturing on a virtual display ({screen})", percent=floor + 1)
            except display_module.VirtualDisplayError as error:
                # Not fatal here: the hub has to work on a machine without Xvfb.
                self._say(f"{error} Capturing on the desktop display.", percent=floor + 1)

            self._capture_with_display(
                run_id=run_id, plan=plan, tooling=tooling, executable=executable,
                floor=floor, ceiling=ceiling)

    def _capture_with_display(
        self,
        *,
        run_id: int,
        plan,
        tooling,
        executable,
        floor: int,
        ceiling: int,
    ) -> None:
        driver = ApplicationDriver(executable)
        driver.start()

        try:
            absent = missing_states(driver.list_states(), surfaces.required_states())

            if absent:
                raise RuntimeError("the application does not offer: " + ", ".join(absent))

            began = time.monotonic()

            def report(entry: dict) -> None:
                fraction = capture_module.progress_fraction(entry)
                left = remaining_seconds(entry, time.monotonic() - began)
                self._say(
                    f"capturing {entry['surface']} at {entry['geometry']} "
                    f"in {entry.get('theme', 'dark')} "
                    f"({entry['done'] + 1} of {entry['total']}"
                    f"{', ' + describe_remaining(left) if left is not None else ''})",
                    percent=int(floor + (ceiling - floor) * fraction),
                )

            result = capture_module.CapturePass(
                store=self.store,
                run_id=run_id,
                driver=driver,
                tooling=tooling,
                image_directory=self.store.path.parent / "images" / f"run-{run_id}",
            ).run(plan, progress=report, should_stop=self._stop.is_set)
        finally:
            driver.stop()

        # Stopped is neither passed nor failed. The export feeds a release gate,
        # and a run reported as failed because somebody pressed stop would
        # either block a release or teach everyone that failures can be ignored.
        # `skipped` and `unavailable` are already treated this way; this joins
        # them.
        if result.get("stopped"):
            state = "stopped"
            summary = (
                f"stopped after {result['captured']} captured, "
                f"{result['not_reached']} not reached"
            )
        else:
            state = "failed" if result["failed"] else "passed"
            summary = f"{result['captured']} captured"

        self._record(
            suite_id="ui-capture",
            state=state,
            cases=result["captured"] + result["failed"],
            failures=result["failed"],
            message=summary,
        )
        self._say(f"UI capture: {summary}", percent=ceiling)

        # Unwind like every other stop, so the run says it was stopped. Left to
        # return normally, a stopped capture reached the end of `_work` and was
        # summarised as "0 of 0 suite(s) passed, 1 could not run" -- true, and
        # no help at all to somebody who has just pressed stop.
        if result.get("stopped"):
            raise RunStopped


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
