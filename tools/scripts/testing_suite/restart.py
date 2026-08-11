#!/usr/bin/env python3
"""Restarting the hub from inside the hub.

The hub imports its modules once and serves its page from disk on every
request, so editing the suite while it is open leaves a current page talking to
stale code -- a control posting to a route that does not exist, or data arriving
in a shape the page stopped expecting. `freshness.suite_staleness_warning` is
what notices; this is the way out, put next to the warning so the answer is
where the problem is reported rather than in a terminal behind the browser.

`os.execv` rather than spawning a second process and exiting: the port is held
by this process, and two hubs briefly fighting over it is a worse failure than
the one being fixed. Exec replaces the image in place, so the listening socket
closes with the old code and the new process binds it as it starts. Python
marks its sockets close-on-exec, so nothing has to unwind first.

Standard library only.
"""

from __future__ import annotations

import os
import sys
import threading

# How long to wait before replacing the process. Long enough for the reply to
# this request to reach the browser -- a restart the page never hears about
# looks like a button that did nothing.
GRACE_SECONDS = 0.4


def restart_command(executable: str, argv: list[str]) -> list[str]:
    """The command that starts this hub again.

    Two adjustments to what was typed:

    A missing subcommand becomes `hub`, because `test-suite` on its own means
    the hub and the parser rewrites it that way too -- exec has no such default.

    `--no-browser` is added, because a restart is a reload of something already
    open. The reason anyone presses it is the tab in front of them, and opening
    a second one answers a question nobody asked.
    """
    arguments = list(argv[1:])

    if not arguments or arguments[0].startswith("-"):
        arguments.insert(0, "hub")

    if "--no-browser" not in arguments:
        arguments.append("--no-browser")

    return [executable, argv[0], *arguments]


def restart_now(command: list[str] | None = None, delay: float = GRACE_SECONDS) -> None:
    """Replace this process with a fresh one, after letting the reply out."""
    chosen = command or restart_command(sys.executable, sys.argv)

    def replace() -> None:
        try:
            os.execv(chosen[0], chosen)
        except OSError as error:  # noqa: BLE001 - there is no caller left to tell
            # The old process survives, which is the safe way for this to fail:
            # the hub keeps answering, stale but working, and says why.
            print(f"could not restart the hub: {error}", file=sys.stderr)

    threading.Timer(delay, replace).start()
