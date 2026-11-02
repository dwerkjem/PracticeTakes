#!/usr/bin/env python3
"""The hub, reachable from another device, still running on this one.

`test-suite hub --remote` serves the hub exactly as it always does -- on
127.0.0.1, on this machine -- and also opens a Cloudflare Tunnel to it, behind
the Cloudflare Access sign-in already used for the feedback dashboard. Whatever
is pressed in the remote browser runs here: the build, the suites, the captures
and any application they start. The remote device only ever has a page.

Three things make the remote URL mean "the hub that started it" and nothing
else:

- **The tunnel lives and dies with the hub.** It is a child process started by
  the hub, stopped when the hub stops, and carried across the hub's own
  in-place restart (`restart.py` execs a fresh hub, which adopts the running
  tunnel instead of starting a second).
- **One machine per tunnel.** Before connecting, the hub asks Cloudflare whether
  the tunnel already has a connector. If one is connected from anywhere else,
  the hub refuses: two connectors on one tunnel share its traffic, so the same
  URL would answer from two machines in turn.
- **Nothing gets past Access.** cloudflared is told to require a valid Access
  token for this application (`originRequest.access`), so it checks the
  signature before a request reaches the hub. The hub then checks the request
  itself (`RequestGuard`): the right host, the Access header present, and a
  same-site origin on anything that changes state.

`configure_remote_hub.py` creates the tunnel, its DNS record and the Access
application once, and writes the settings this module reads.

Standard library only.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
from typing import Callable

# Where the one-time setup writes its settings. Outside the repository: it names
# a credentials file and an account's Access audience, which belong to this
# machine and this account, not to the project.
CONFIG_NAME = "remote-hub.json"

# Carried across the hub's in-place restart, which execs a fresh process with
# the same environment. The new hub adopts this tunnel rather than starting a
# second connector beside it.
TUNNEL_PID_VARIABLE = "PRACTICE_TAKES_HUB_TUNNEL_PID"

# How long to wait for Cloudflare to forget a connector this machine just
# stopped: up to ten checks, two seconds apart.
ORPHAN_SETTLE_TRIES = 10
ORPHAN_SETTLE_SECONDS = 2.0

# The header Cloudflare Access adds to every request it lets through.
ACCESS_HEADER = "Cf-Access-Jwt-Assertion"

SETUP_HINT = (
    "Set it up once with:\n"
    "  uv run python tools/scripts/testing_suite/configure_remote_hub.py "
    "--hostname tests.example.org\n"
    "See docs/development/quality/TESTING_SUITE.md § Using the hub remotely."
)


class RemoteError(Exception):
    """Something that stops the hub going remote. Said, never swallowed."""


def config_path(environ: dict[str, str] | None = None) -> Path:
    """`$XDG_CONFIG_HOME/practice-takes/remote-hub.json`."""
    environ = os.environ if environ is None else environ
    base = environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")

    return Path(base) / "practice-takes" / CONFIG_NAME


@dataclass(frozen=True)
class RemoteConfig:
    """What the setup wrote: one tunnel, one hostname, one Access application."""

    tunnel_id: str
    tunnel_name: str
    credentials_file: str
    hostname: str
    team_name: str
    aud_tag: str

    @classmethod
    def load(cls, path: Path) -> RemoteConfig:
        try:
            raw = json.loads(path.read_text())
        except FileNotFoundError:
            raise RemoteError(f"The hub has not been set up for remote use.\n{SETUP_HINT}") from None
        except (OSError, ValueError) as error:
            raise RemoteError(f"{path} could not be read: {error}") from None

        missing = [name for name in cls.__dataclass_fields__ if not str(raw.get(name, "")).strip()]

        if missing:
            raise RemoteError(
                f"{path} is missing {', '.join(missing)}. Run the setup again.\n{SETUP_HINT}"
            )

        return cls(**{name: str(raw[name]).strip() for name in cls.__dataclass_fields__})

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.__dict__, indent=2) + "\n")
        path.chmod(0o600)

    @property
    def url(self) -> str:
        return f"https://{self.hostname}/"


def cloudflared_config(config: RemoteConfig, port: int) -> str:
    """The tunnel's ingress: this hostname to this hub, Access required.

    Written per start rather than once, because the port is the hub's choice
    at the moment it starts. JSON is valid YAML, so no YAML writer is needed.
    """
    document = {
        "tunnel": config.tunnel_id,
        "credentials-file": config.credentials_file,
        "no-autoupdate": True,
        "ingress": [
            {
                "hostname": config.hostname,
                "service": f"http://127.0.0.1:{port}",
                "originRequest": {
                    "access": {
                        "required": True,
                        "teamName": config.team_name,
                        "audTag": [config.aud_tag],
                    }
                },
            },
            {"service": "http_status:404"},
        ],
    }

    return json.dumps(document, indent=2) + "\n"


def connected_elsewhere(info: dict) -> list[str]:
    """The connectors `cloudflared tunnel info --output json` reports as live.

    A connector with no edge connections is one that has gone away and not yet
    been forgotten, and does not share traffic. Each live one is described by
    its origin address, which is what tells a person which machine it is.
    """
    described: list[str] = []

    for connector in info.get("conns") or []:
        if not connector.get("conns"):
            continue

        where = connector.get("origin_ip") or "an unknown address"
        version = connector.get("version") or "?"
        described.append(f"{where} (cloudflared {version})")

    return described


Runner = Callable[[list[str]], subprocess.CompletedProcess]


def _run(command: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)


def tunnel_info(cloudflared: str, config: RemoteConfig, run: Runner = _run) -> dict:
    result = run([cloudflared, "tunnel", "info", "--output", "json", config.tunnel_id])

    if result.returncode != 0:
        raise RemoteError(
            f"cloudflared could not describe tunnel {config.tunnel_name}: "
            f"{(result.stderr or result.stdout).strip()}"
        )

    try:
        return json.loads(result.stdout)
    except ValueError as error:
        raise RemoteError(f"cloudflared described the tunnel in a form this does not read: {error}") from None


def orphaned_tunnels(tunnel_id: str, proc: Path = Path("/proc")) -> list[int]:
    """cloudflared processes a previous hub on this machine left running.

    A hub that was killed rather than stopped cannot stop its tunnel, and the
    next start would then find the tunnel "connected elsewhere" -- from this
    very machine. Recognised by the private config directory this module names
    and the tunnel id, so nothing else running cloudflared is touched: the
    machine's other tunnels are none of this module's business.
    """
    found: list[int] = []

    for entry in proc.glob("[0-9]*"):
        try:
            if (entry / "comm").read_text().strip() != "cloudflared":
                continue

            arguments = (entry / "cmdline").read_bytes().split(b"\0")
        except OSError:
            continue

        text = [part.decode("utf-8", "replace") for part in arguments]

        if tunnel_id in text and any("practice-takes-tunnel-" in part for part in text):
            found.append(int(entry.name))

    return sorted(found)


def _is_cloudflared(pid: int, proc: Path = Path("/proc")) -> bool:
    try:
        return (proc / str(pid) / "comm").read_text().strip() == "cloudflared"
    except OSError:
        return False


class Tunnel:
    """The cloudflared process that connects this hub, for as long as it runs."""

    def __init__(
        self,
        config: RemoteConfig,
        port: int,
        *,
        cloudflared: str | None = None,
        run: Runner = _run,
        environ: dict[str, str] | None = None,
        is_cloudflared: Callable[[int], bool] = _is_cloudflared,
        popen: Callable[..., subprocess.Popen] = subprocess.Popen,
        orphans: Callable[[str], list[int]] = orphaned_tunnels,
        kill: Callable[[int, int], None] = os.kill,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self.config = config
        self.port = port
        self.cloudflared = cloudflared or shutil.which("cloudflared") or ""
        self.run = run
        self.environ = os.environ if environ is None else environ
        self.is_cloudflared = is_cloudflared
        self.popen = popen
        self.orphans = orphans
        self.kill = kill
        self.sleep = sleep
        self.process: subprocess.Popen | None = None
        self.adopted_pid: int | None = None
        self.directory: tempfile.TemporaryDirectory | None = None

    def start(self) -> str:
        """Connect, and say how. Raises RemoteError rather than half-starting."""
        if not self.cloudflared:
            raise RemoteError(
                "cloudflared is not installed. Install it from "
                "https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/downloads/"
            )

        # A restart of this same hub: the tunnel it started is still running
        # and already pointed at this port. Keep it.
        inherited = self.environ.get(TUNNEL_PID_VARIABLE, "")

        if inherited.isdigit() and self.is_cloudflared(int(inherited)):
            self.adopted_pid = int(inherited)

            return f"Kept the running tunnel (cloudflared pid {inherited})."

        left_behind = self.orphans(self.config.tunnel_id)

        for pid in left_behind:
            try:
                self.kill(pid, signal.SIGTERM)
            except OSError:
                pass

        elsewhere = connected_elsewhere(tunnel_info(self.cloudflared, self.config, self.run))

        # Cloudflare forgets a stopped connector a few seconds after it goes.
        # Only worth waiting for when this machine just stopped one; a live
        # connector somewhere else is not going to leave.
        for _ in range(ORPHAN_SETTLE_TRIES if left_behind else 0):
            if not elsewhere:
                break

            self.sleep(ORPHAN_SETTLE_SECONDS)
            elsewhere = connected_elsewhere(tunnel_info(self.cloudflared, self.config, self.run))

        if elsewhere:
            raise RemoteError(
                f"Tunnel {self.config.tunnel_name} is already connected from "
                f"{', '.join(elsewhere)}. Stop the hub there first: two machines on one "
                "tunnel would share its URL, and the page would answer from either."
            )

        # Private to this user: the file names the credentials the tunnel runs
        # with, even though it does not contain them.
        self.directory = tempfile.TemporaryDirectory(prefix="practice-takes-tunnel-")
        Path(self.directory.name).chmod(0o700)
        config_file = Path(self.directory.name) / "config.yml"
        config_file.write_text(cloudflared_config(self.config, self.port))

        self.process = self.popen(
            [self.cloudflared, "tunnel", "--config", str(config_file), "run", self.config.tunnel_id],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
        )
        self.environ[TUNNEL_PID_VARIABLE] = str(self.process.pid)

        return f"Tunnel {self.config.tunnel_name} started (cloudflared pid {self.process.pid})."

    def stop(self) -> None:
        if self.process is not None:
            self.process.terminate()

            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()

            self.process = None
        elif self.adopted_pid is not None:
            try:
                self.kill(self.adopted_pid, signal.SIGTERM)
            except OSError:
                pass

            self.adopted_pid = None

        self.environ.pop(TUNNEL_PID_VARIABLE, None)

        if self.directory is not None:
            self.directory.cleanup()
            self.directory = None


def stop_on_signals(install: Callable[[int, object], object] = signal.signal) -> None:
    """Make SIGTERM, SIGHUP and SIGINT end the hub the way Ctrl-C does.

    The tunnel is stopped by the hub's own shutdown, which runs on
    KeyboardInterrupt. Python only raises that for SIGINT, and not even for
    SIGINT when the hub was started in the background, where it inherits the
    signal ignored. `kill`, a closed terminal, or a service manager stopping the
    hub would otherwise end it with the tunnel still running -- connected,
    pointing at a port nothing is listening on, and blocking the next start
    until it is found.
    """

    def interrupt(signum: int, frame: object) -> None:
        raise KeyboardInterrupt

    for name in ("SIGTERM", "SIGHUP", "SIGINT"):
        number = getattr(signal, name, None)

        if number is not None:
            install(number, interrupt)


@dataclass(frozen=True)
class RequestGuard:
    """Which requests the hub answers, decided before any route runs.

    Local use is unchanged except for one check: the Host header has to name
    this hub. That alone stops a web page elsewhere from reaching the hub by
    pointing a name it controls at 127.0.0.1.

    A request for the remote hostname has come through the tunnel, so it must
    carry the Access header (cloudflared has already verified its signature),
    and a request that changes something must come from the hub's own page.
    The Access cookie is SameSite=Lax, so another site cannot make the browser
    send it with a POST anyway; the origin check means that is not the only
    thing standing in the way.
    """

    port: int
    remote_hostname: str = ""

    def local_hosts(self) -> set[str]:
        return {f"127.0.0.1:{self.port}", f"localhost:{self.port}"}

    def refusal(self, method: str, headers) -> str:
        """Why this request is refused, or "" to let it through."""
        host = (headers.get("Host") or "").strip().lower()

        if host in self.local_hosts():
            origin = (headers.get("Origin") or "").strip().lower()

            if method == "POST" and origin and origin not in {f"http://{h}" for h in self.local_hosts()}:
                return "this request did not come from the hub's own page"

            return ""

        if self.remote_hostname and host == self.remote_hostname.lower():
            if not (headers.get(ACCESS_HEADER) or "").strip():
                return "remote requests must come through Cloudflare Access"

            if method == "POST":
                origin = (headers.get("Origin") or "").strip().lower()

                if origin != f"https://{self.remote_hostname.lower()}":
                    return "this request did not come from the hub's own page"

            return ""

        return "this hub does not answer to that host name"
