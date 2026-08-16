#!/usr/bin/env python3
"""Every application a run started, and nobody else's.

A parallel run starts several applications, and some of them go wrong: one comes
up without an input device and never answers, another has to be replaced. Ending
those is necessary. Ending them *by name* is not the same thing, and the
difference matters on the machine this runs on, where the developer very
reasonably has Practice Takes open while the suite works.

So the run keeps the process ids it started. Teardown ends those and nothing
else, and a report about what is running says which ones were the run's.

The other half of the same fact: an application the run did not start still
holds the input device, and every worker that tries to open it afterwards
contends with it. That is worth saying out loud at the start of a run rather
than discovering as a worker that inexplicably never replied — so this can also
name the instances a run is competing with, without touching them.

Linux only, by reading `/proc`. The capture pass is Linux-only anyway: it drives
X utilities compiled against Xlib.

Standard library only.
"""

from __future__ import annotations

from pathlib import Path
import threading

PROCESS_ROOT = Path("/proc")

# What the application is called in the process table.
APPLICATION = "PracticeTakes"


def running_instances(root: Path = PROCESS_ROOT) -> set[int]:
    """Every process id on this machine that is the application.

    `comm` rather than the command line, because the command line of anything
    that *mentions* the application -- a build, an editor, this suite -- would
    match too, and a check that catches its own tooling is worse than no check.
    """
    found: set[int] = set()

    for entry in root.glob("[0-9]*"):
        try:
            if (entry / "comm").read_text().strip() == APPLICATION:
                found.add(int(entry.name))
        except (OSError, ValueError):
            # Gone between the listing and the read, which is normal.
            continue

    return found


class Fleet:
    """The applications one run started.

    A worker registers its application as soon as it has one and removes it when
    it has finished with it, so `pids` is what this run currently owns. Nothing
    here ends a process that was not registered.
    """

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._drivers: dict[int, object] = {}
        # Everything ever registered, kept after removal so a report at the end
        # can still tell the run's processes from somebody else's.
        self._seen: set[int] = set()

    def add(self, driver) -> None:
        pid = getattr(driver, "pid", None)

        if pid is None:
            return

        with self._lock:
            self._drivers[int(pid)] = driver
            self._seen.add(int(pid))

    def remove(self, driver) -> None:
        pid = getattr(driver, "pid", None)

        with self._lock:
            for known, held in list(self._drivers.items()):
                if held is driver or (pid is not None and known == int(pid)):
                    del self._drivers[known]

    def pids(self) -> set[int]:
        with self._lock:
            return set(self._drivers)

    def started(self) -> set[int]:
        with self._lock:
            return set(self._seen)

    def foreign(self, root: Path = PROCESS_ROOT) -> set[int]:
        """Instances running that this run did not start.

        These are not a problem to solve here -- somebody is using their
        computer -- but they hold the input device, and a worker that cannot
        open it comes up and never answers. Naming them turns a mystifying
        failure into a sentence.
        """
        return running_instances(root) - self.started()

    def shut_down(self) -> int:
        """End what this run started, and only that. Returns how many."""
        with self._lock:
            drivers = list(self._drivers.values())
            self._drivers.clear()

        for driver in drivers:
            try:
                driver.kill()
            except Exception:  # noqa: BLE001 - teardown reports nothing to nobody
                continue

        return len(drivers)


def contention_warning(foreign: set[int]) -> str:
    """What to say about instances the run is competing with, or ""."""
    if not foreign:
        return ""

    return (
        f"{len(foreign)} other Practice Takes "
        f"{'instance is' if len(foreign) == 1 else 'instances are'} already "
        f"running (pid {', '.join(str(pid) for pid in sorted(foreign))}). "
        f"They hold the input device, so a worker may come up without one and "
        f"stop answering; its share goes to the others. Close them for a full "
        f"speed run — nothing here will."
    )
