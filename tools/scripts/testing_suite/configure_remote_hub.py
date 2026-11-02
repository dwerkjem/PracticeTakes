#!/usr/bin/env python3
"""Set up the testing-suite hub for remote use, once per machine.

Creates or reuses, on the same Cloudflare account and Zero Trust team as the
feedback dashboard:

1. **A named Cloudflare Tunnel** for this machine (`cloudflared tunnel create`),
   whose credentials stay in `~/.cloudflared/` as cloudflared keeps them.
2. **A DNS record** sending the chosen hostname to that tunnel
   (`cloudflared tunnel route dns`). An existing record pointing somewhere
   else is reported, never overwritten.
3. **A Cloudflare Access application** on that hostname that admits only the
   administrator's email -- the same identity the feedback dashboard admits.

Then writes `~/.config/practice-takes/remote-hub.json`, which
`test-suite hub --remote` reads. Running it again updates rather than
duplicates.

Credentials come from `src/services/feedback-intake/.env`, the file the feedback
setup already uses; variables exported by the caller win:

  CLOUDFLARE_ACCOUNT_ID       the account holding the Zero Trust team
  CLOUDFLARE_SETUP_API_TOKEN  Access: Apps and Policies Write (falls back to
                              CLOUDFLARE_API_TOKEN)
  ADMIN_EMAIL                 the only identity the hub admits

`cloudflared` must already be logged in to the zone (`cloudflared tunnel
login`), which is what creating a tunnel and its DNS record needs.

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Callable
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))

import remote  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[3]
FEEDBACK_ENVIRONMENT = REPOSITORY / "src" / "services" / "feedback-intake" / ".env"
API_BASE = "https://api.cloudflare.com/client/v4"
APPLICATION_NAME = "Practice Takes testing hub"

# Long enough for an afternoon of reviewing without signing in again, short
# enough that a lost phone is not a standing key to a machine that builds code.
SESSION_DURATION = "12h"

HOSTNAME = re.compile(r"^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)+$")
EMAIL = re.compile(r"^[^\s@,]+@[^\s@,]+\.[^\s@,]+$")


class SetupError(Exception):
    """Something that stops the setup. Said plainly, then exit 1."""


def read_environment(path: Path, environ: dict[str, str]) -> dict[str, str]:
    """NAME=VALUE lines from `path`, under whatever the caller exported."""
    values: dict[str, str] = {}

    if path.is_file():
        for line in path.read_text().splitlines():
            line = line.strip()

            if not line or line.startswith("#") or "=" not in line:
                continue

            if line.startswith("export "):
                line = line[len("export ") :]

            name, value = line.split("=", 1)
            values[name.strip()] = value.strip().strip('"').strip("'")

    values.update({name: value for name, value in environ.items() if value})

    return values


def default_tunnel_name(node: str | None = None) -> str:
    """One tunnel per machine, named for it, so `tunnel list` says whose it is."""
    machine = re.sub(r"[^a-z0-9-]+", "-", (node or platform.node() or "machine").lower()).strip("-")

    return f"practice-takes-hub-{machine or 'machine'}"


Api = Callable[[str, str, dict | None], dict]


def cloudflare_api(token: str) -> Api:
    def request(method: str, path: str, body: dict | None = None) -> dict:
        data = None if body is None else json.dumps(body).encode("utf-8")
        call = urllib.request.Request(
            API_BASE + path,
            data=data,
            method=method,
            headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        )

        try:
            with urllib.request.urlopen(call, timeout=30) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            try:
                return json.loads(error.read().decode("utf-8"))
            except ValueError:
                return {"success": False, "errors": [{"message": f"HTTP {error.code}"}]}

    return request


def require(response: dict, doing: str) -> dict:
    if not response.get("success"):
        messages = "; ".join(e.get("message", "?") for e in response.get("errors") or []) or "no reason given"

        raise SetupError(f"Cloudflare could not {doing}: {messages}")

    return response


def application_payload(hostname: str, email: str) -> dict:
    return {
        "name": APPLICATION_NAME,
        "type": "self_hosted",
        "domain": hostname,
        "session_duration": SESSION_DURATION,
        "app_launcher_visible": False,
        "policies": [
            {
                "name": "Allow the testing-suite administrator",
                "decision": "allow",
                "precedence": 1,
                "include": [{"email": {"email": email}}],
            }
        ],
    }


def ensure_access_application(api: Api, account: str, hostname: str, email: str) -> tuple[str, str]:
    """(team name, audience tag) for an Access application on `hostname`."""
    organization = require(api("GET", f"/accounts/{account}/access/organizations", None), "read the Zero Trust organization")
    auth_domain = (organization.get("result") or {}).get("auth_domain") or ""

    if not auth_domain.endswith(".cloudflareaccess.com"):
        raise SetupError("The account has no Zero Trust team domain to sign in through.")

    team = auth_domain[: -len(".cloudflareaccess.com")]

    listed = require(api("GET", f"/accounts/{account}/access/apps?per_page=100", None), "list Access applications")
    matching = [
        app
        for app in listed.get("result") or []
        if app.get("type") == "self_hosted"
        and (app.get("domain") == hostname or app.get("name") == APPLICATION_NAME)
    ]

    if len(matching) > 1:
        raise SetupError(
            f"More than one Access application is named {APPLICATION_NAME!r} or protects "
            f"{hostname}. Remove the extra ones in Cloudflare Zero Trust and run this again."
        )

    payload = application_payload(hostname, email)

    if matching:
        saved = require(
            api("PUT", f"/accounts/{account}/access/apps/{matching[0]['id']}", payload),
            "update the Access application",
        )
    else:
        saved = require(api("POST", f"/accounts/{account}/access/apps", payload), "create the Access application")

    audience = (saved.get("result") or {}).get("aud") or ""

    if not audience:
        raise SetupError("Cloudflare did not return the Access application's audience tag.")

    return team, audience


Run = Callable[[list[str]], subprocess.CompletedProcess]


def _run(command: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)


def ensure_tunnel(cloudflared: str, name: str, run: Run = _run) -> str:
    """The tunnel's id, creating it if this machine has none by that name."""
    listed = run([cloudflared, "tunnel", "list", "--output", "json", "--name", name])

    if listed.returncode != 0:
        raise SetupError(
            "cloudflared could not list tunnels. Has it been logged in with "
            f"`cloudflared tunnel login`?\n{(listed.stderr or listed.stdout).strip()}"
        )

    for tunnel in json.loads(listed.stdout or "[]") or []:
        # `tunnel list` leaves deleted tunnels out unless asked for them.
        if tunnel.get("name") == name:
            return str(tunnel["id"])

    created = run([cloudflared, "tunnel", "--output", "json", "create", name])

    if created.returncode != 0:
        raise SetupError(f"cloudflared could not create tunnel {name}: {(created.stderr or created.stdout).strip()}")

    return str(json.loads(created.stdout)["id"])


def route_dns(cloudflared: str, tunnel_id: str, hostname: str, run: Run = _run) -> str:
    routed = run([cloudflared, "tunnel", "route", "dns", tunnel_id, hostname])
    said = (routed.stderr or routed.stdout).strip()

    if routed.returncode != 0:
        raise SetupError(
            f"{hostname} could not be pointed at the tunnel: {said}\n"
            "If a record by that name already exists for something else, choose another "
            "hostname; this setup does not overwrite DNS."
        )

    return said


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--hostname", required=True, help="Where the hub will be reached, e.g. tests.example.org")
    parser.add_argument("--tunnel-name", default=default_tunnel_name())
    parser.add_argument("--environment-file", type=Path, default=FEEDBACK_ENVIRONMENT)
    arguments = parser.parse_args(argv)

    hostname = arguments.hostname.strip().lower()

    try:
        if not HOSTNAME.match(hostname):
            raise SetupError("--hostname must be a host name with no scheme, port or path.")

        values = read_environment(arguments.environment_file, dict(os.environ))
        account = values.get("CLOUDFLARE_ACCOUNT_ID", "")
        token = values.get("CLOUDFLARE_SETUP_API_TOKEN") or values.get("CLOUDFLARE_API_TOKEN", "")
        email = values.get("ADMIN_EMAIL", "").lower()

        if not re.fullmatch(r"[0-9a-fA-F]{32}", account):
            raise SetupError("CLOUDFLARE_ACCOUNT_ID must be set to the account's 32-character id.")

        if not token:
            raise SetupError("CLOUDFLARE_SETUP_API_TOKEN must be set (Access: Apps and Policies Write).")

        if not EMAIL.match(email):
            raise SetupError("ADMIN_EMAIL must be set to exactly one email address.")

        cloudflared = shutil.which("cloudflared")

        if not cloudflared:
            raise SetupError("cloudflared is not installed.")

        tunnel_id = ensure_tunnel(cloudflared, arguments.tunnel_name)
        print(f"Tunnel {arguments.tunnel_name}: {tunnel_id}")

        print(route_dns(cloudflared, tunnel_id, hostname) or f"{hostname} routed to the tunnel.")

        team, audience = ensure_access_application(cloudflare_api(token), account, hostname, email)
        print(f"Access application {APPLICATION_NAME!r} admits {email} (team {team}).")

        credentials = Path.home() / ".cloudflared" / f"{tunnel_id}.json"

        if not credentials.is_file():
            raise SetupError(
                f"The tunnel's credentials are not at {credentials}. They are written on "
                "the machine that created the tunnel; run this setup there."
            )

        config = remote.RemoteConfig(
            tunnel_id=tunnel_id,
            tunnel_name=arguments.tunnel_name,
            credentials_file=str(credentials),
            hostname=hostname,
            team_name=team,
            aud_tag=audience,
        )
        config.save(remote.config_path())
    except SetupError as error:
        print(f"Error: {error}", file=sys.stderr)

        return 1

    print(f"Saved {remote.config_path()}.")
    print("Start the hub with:  uv run test-suite hub --remote")
    print(f"Then open {config.url} from anywhere and sign in as {email}.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
