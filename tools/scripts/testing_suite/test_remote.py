#!/usr/bin/env python3
"""Tests for opening the hub remotely: the guard, the tunnel, and the setup.

Nothing here touches Cloudflare or starts cloudflared. The tunnel and the setup
take their process runner and API as arguments, so every decision they make --
refuse, adopt, stop an orphan, create or update -- is checked against fakes.
The guard is checked twice: as a function, and through a real server bound to
port 0, because a guard that is right but never consulted protects nothing.
"""

from __future__ import annotations

import http.client
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import configure_remote_hub as setup  # noqa: E402
import remote  # noqa: E402
import restart  # noqa: E402
import server as server_module  # noqa: E402
from store import Store  # noqa: E402

HOSTNAME = "tests.example.org"


def config() -> remote.RemoteConfig:
    return remote.RemoteConfig(
        tunnel_id="11111111-2222-3333-4444-555555555555",
        tunnel_name="practice-takes-hub-box",
        credentials_file="/home/me/.cloudflared/11111111-2222-3333-4444-555555555555.json",
        hostname=HOSTNAME,
        team_name="team",
        aud_tag="aud123",
    )


def completed(stdout: str = "", returncode: int = 0, stderr: str = "") -> subprocess.CompletedProcess:
    return subprocess.CompletedProcess([], returncode, stdout=stdout, stderr=stderr)


def info(*connectors: dict) -> str:
    return json.dumps({"id": config().tunnel_id, "name": config().tunnel_name, "conns": list(connectors)})


LIVE = {"origin_ip": "203.0.113.7", "version": "2026.10.0", "conns": [{"colo_name": "yyz01"}]}
GONE = {"origin_ip": "203.0.113.7", "version": "2026.10.0", "conns": []}


class GuardTests(unittest.TestCase):
    guard = remote.RequestGuard(8730, HOSTNAME)

    def refusal(self, method: str, **headers: str) -> str:
        return self.guard.refusal(method, {k.replace("_", "-"): v for k, v in headers.items()})

    def test_local_use_is_unchanged(self) -> None:
        self.assertEqual(self.refusal("GET", Host="127.0.0.1:8730"), "")
        self.assertEqual(self.refusal("GET", Host="localhost:8730"), "")
        self.assertEqual(self.refusal("POST", Host="127.0.0.1:8730"), "")
        self.assertEqual(self.refusal("POST", Host="127.0.0.1:8730", Origin="http://127.0.0.1:8730"), "")

    def test_a_name_pointed_at_this_machine_is_refused(self) -> None:
        """DNS rebinding: evil.example resolving to 127.0.0.1 is not this hub."""
        self.assertIn("host name", self.refusal("GET", Host="evil.example:8730"))
        self.assertIn("host name", self.refusal("GET", Host="127.0.0.1:9999"))
        self.assertIn("host name", self.refusal("GET"))

    def test_a_local_post_from_another_page_is_refused(self) -> None:
        self.assertIn("own page", self.refusal("POST", Host="127.0.0.1:8730", Origin="https://evil.example"))

    def test_remote_requests_need_access(self) -> None:
        self.assertIn("Access", self.refusal("GET", Host=HOSTNAME))
        self.assertEqual(self.refusal("GET", Host=HOSTNAME, Cf_Access_Jwt_Assertion="token"), "")

    def test_a_remote_post_must_come_from_the_hub_page(self) -> None:
        signed = {"Host": HOSTNAME, "Cf-Access-Jwt-Assertion": "token"}

        self.assertIn("own page", self.guard.refusal("POST", signed))
        self.assertIn("own page", self.guard.refusal("POST", {**signed, "Origin": "https://evil.example"}))
        self.assertIn("own page", self.guard.refusal("POST", {**signed, "Origin": f"http://{HOSTNAME}"}))
        self.assertEqual(self.guard.refusal("POST", {**signed, "Origin": f"https://{HOSTNAME}"}), "")

    def test_a_hub_not_opened_remotely_answers_no_remote_name(self) -> None:
        local_only = remote.RequestGuard(8730)

        self.assertIn(
            "host name", local_only.refusal("GET", {"Host": HOSTNAME, "Cf-Access-Jwt-Assertion": "token"})
        )


class GuardedServerTests(unittest.TestCase):
    """The same rules, through a real server: the guard has to be consulted."""

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.store = Store.open(Path(self.directory.name) / "verification.db")
        self.httpd = server_module.serve(self.store, None, port=0, remote_hostname=HOSTNAME)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def tearDown(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()
        self.store.close()
        self.directory.cleanup()

    def status(self, method: str, path: str, headers: dict[str, str], body: str | None = None) -> int:
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        connection.putrequest(method, path, skip_host=True)

        for name, value in headers.items():
            connection.putheader(name, value)

        data = (body or "").encode("utf-8")
        connection.putheader("Content-Length", str(len(data)))
        connection.endheaders(data)
        status = connection.getresponse().status
        connection.close()

        return status

    def test_local_requests_are_answered(self) -> None:
        self.assertEqual(self.status("GET", "/", {"Host": f"127.0.0.1:{self.port}"}), 200)

    def test_another_host_name_is_refused(self) -> None:
        self.assertEqual(self.status("GET", "/", {"Host": f"evil.example:{self.port}"}), 403)

    def test_remote_requests_without_access_are_refused(self) -> None:
        self.assertEqual(self.status("GET", "/", {"Host": HOSTNAME}), 403)

    def test_remote_requests_through_access_are_answered(self) -> None:
        signed = {"Host": HOSTNAME, "Cf-Access-Jwt-Assertion": "token"}

        self.assertEqual(self.status("GET", "/", signed), 200)

    def test_a_remote_post_from_another_site_never_reaches_a_route(self) -> None:
        signed = {"Host": HOSTNAME, "Cf-Access-Jwt-Assertion": "token", "Origin": "https://evil.example"}

        self.assertEqual(self.status("POST", "/api/stop-run", signed, "{}"), 403)


class ConfigTests(unittest.TestCase):
    def test_config_lives_under_xdg_config_home(self) -> None:
        self.assertEqual(
            remote.config_path({"XDG_CONFIG_HOME": "/cfg"}), Path("/cfg/practice-takes/remote-hub.json")
        )

    def test_a_missing_config_says_how_to_set_up(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(remote.RemoteError) as raised:
                remote.RemoteConfig.load(Path(directory) / "absent.json")

        self.assertIn("configure_remote_hub.py", str(raised.exception))

    def test_an_incomplete_config_names_what_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "remote-hub.json"
            path.write_text(json.dumps({"tunnel_id": "x", "hostname": HOSTNAME}))

            with self.assertRaises(remote.RemoteError) as raised:
                remote.RemoteConfig.load(path)

        self.assertIn("aud_tag", str(raised.exception))

    def test_saved_config_round_trips_and_is_private(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested" / "remote-hub.json"
            config().save(path)

            self.assertEqual(remote.RemoteConfig.load(path), config())
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_cloudflared_is_told_to_require_access(self) -> None:
        document = json.loads(remote.cloudflared_config(config(), 8731))
        first, last = document["ingress"]

        self.assertEqual(document["tunnel"], config().tunnel_id)
        self.assertEqual(first["hostname"], HOSTNAME)
        self.assertEqual(first["service"], "http://127.0.0.1:8731")
        self.assertEqual(
            first["originRequest"]["access"], {"required": True, "teamName": "team", "audTag": ["aud123"]}
        )
        self.assertEqual(last, {"service": "http_status:404"})

    def test_only_connectors_with_edge_connections_count(self) -> None:
        self.assertEqual(remote.connected_elsewhere(json.loads(info(GONE))), [])
        self.assertEqual(
            remote.connected_elsewhere(json.loads(info(GONE, LIVE))), ["203.0.113.7 (cloudflared 2026.10.0)"]
        )
        self.assertEqual(remote.connected_elsewhere({}), [])


class FakeProcess:
    pid = 4242

    def __init__(self) -> None:
        self.terminated = False

    def terminate(self) -> None:
        self.terminated = True

    def wait(self, timeout: float) -> int:
        return 0


class TunnelTests(unittest.TestCase):
    def make(self, infos: list[str], **overrides) -> tuple[remote.Tunnel, dict]:
        seen: dict = {"commands": [], "popen": [], "killed": [], "slept": 0}
        replies = list(infos)

        def run(command: list[str]) -> subprocess.CompletedProcess:
            seen["commands"].append(command)

            return completed(replies.pop(0) if len(replies) > 1 else replies[0])

        def popen(command: list[str], **_: object) -> FakeProcess:
            seen["popen"].append(command)
            seen["config"] = Path(command[command.index("--config") + 1]).read_text()
            seen["process"] = FakeProcess()

            return seen["process"]

        def sleep(_: float) -> None:
            seen["slept"] += 1

        arguments = dict(
            cloudflared="/usr/bin/cloudflared",
            run=run,
            environ={},
            is_cloudflared=lambda pid: False,
            popen=popen,
            orphans=lambda tunnel_id: [],
            kill=lambda pid, sig: seen["killed"].append(pid),
            sleep=sleep,
        )
        arguments.update(overrides)

        tunnel = remote.Tunnel(config(), 8730, **arguments)
        self.addCleanup(tunnel.stop)

        return tunnel, seen

    def test_an_idle_tunnel_is_connected_to_this_hub(self) -> None:
        tunnel, seen = self.make([info(GONE)])

        tunnel.start()

        self.assertEqual(seen["popen"][0][:3], ["/usr/bin/cloudflared", "tunnel", "--config"])
        self.assertEqual(seen["popen"][0][-2:], ["run", config().tunnel_id])
        self.assertIn('"http://127.0.0.1:8730"', seen["config"])
        self.assertEqual(tunnel.environ[remote.TUNNEL_PID_VARIABLE], "4242")

    def test_a_tunnel_connected_elsewhere_is_refused(self) -> None:
        """Two connectors share one URL; the page would answer from either."""
        tunnel, seen = self.make([info(LIVE)])

        with self.assertRaises(remote.RemoteError) as raised:
            tunnel.start()

        self.assertIn("203.0.113.7", str(raised.exception))
        self.assertEqual(seen["popen"], [])
        self.assertEqual(seen["slept"], 0)

    def test_a_restarted_hub_keeps_its_tunnel(self) -> None:
        tunnel, seen = self.make(
            [info(LIVE)], environ={remote.TUNNEL_PID_VARIABLE: "77"}, is_cloudflared=lambda pid: pid == 77
        )

        self.assertIn("Kept", tunnel.start())
        self.assertEqual(seen["commands"], [])
        self.assertEqual(seen["popen"], [])

        tunnel.stop()

        self.assertEqual(seen["killed"], [77])
        self.assertNotIn(remote.TUNNEL_PID_VARIABLE, tunnel.environ)

    def test_a_stale_pid_is_not_adopted(self) -> None:
        tunnel, seen = self.make([info(GONE)], environ={remote.TUNNEL_PID_VARIABLE: "77"})

        tunnel.start()

        self.assertEqual(len(seen["popen"]), 1)

    def test_a_tunnel_a_killed_hub_left_behind_is_stopped_and_waited_out(self) -> None:
        tunnel, seen = self.make([info(LIVE), info(LIVE), info(GONE)], orphans=lambda tunnel_id: [91])

        tunnel.start()

        self.assertEqual(seen["killed"], [91])
        self.assertEqual(seen["slept"], 2)
        self.assertEqual(len(seen["popen"]), 1)

    def test_stopping_ends_the_process_and_its_config(self) -> None:
        tunnel, seen = self.make([info(GONE)])
        tunnel.start()
        directory = Path(tunnel.directory.name)

        tunnel.stop()

        self.assertTrue(seen["process"].terminated)
        self.assertFalse(directory.exists())
        self.assertNotIn(remote.TUNNEL_PID_VARIABLE, tunnel.environ)

    def test_no_cloudflared_is_said(self) -> None:
        tunnel, _ = self.make([info(GONE)], cloudflared="")
        tunnel.cloudflared = ""

        with self.assertRaises(remote.RemoteError) as raised:
            tunnel.start()

        self.assertIn("not installed", str(raised.exception))

    def test_orphans_are_only_this_modules_tunnels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            proc = Path(directory)

            def process(pid: int, comm: str, *arguments: str) -> None:
                (proc / str(pid)).mkdir()
                (proc / str(pid) / "comm").write_text(comm + "\n")
                (proc / str(pid) / "cmdline").write_bytes(b"\0".join(a.encode() for a in arguments))

            tunnel_id = config().tunnel_id
            process(10, "cloudflared", "cloudflared", "tunnel", "--config", "/tmp/practice-takes-tunnel-ab/config.yml", "run", tunnel_id)
            process(11, "cloudflared", "cloudflared", "--no-autoupdate", "tunnel", "run", "--token-file", "/etc/cloudflared/token")
            process(12, "cloudflared", "cloudflared", "tunnel", "--config", "/tmp/practice-takes-tunnel-cd/config.yml", "run", "another-id")
            process(13, "python3", "python3", "practice-takes-tunnel-", tunnel_id)

            self.assertEqual(remote.orphaned_tunnels(tunnel_id, proc), [10])


class SignalTests(unittest.TestCase):
    def test_every_way_of_stopping_the_hub_runs_its_shutdown(self) -> None:
        """Otherwise `kill` or a closed terminal leaves the tunnel running."""
        installed: dict[int, object] = {}

        remote.stop_on_signals(lambda number, handler: installed.__setitem__(number, handler))

        self.assertEqual(
            set(installed), {remote.signal.SIGTERM, remote.signal.SIGHUP, remote.signal.SIGINT}
        )

        for handler in installed.values():
            with self.assertRaises(KeyboardInterrupt):
                handler(remote.signal.SIGTERM, None)


class RestartTests(unittest.TestCase):
    def test_a_restart_stays_remote(self) -> None:
        command = restart.restart_command("/py", ["test-suite", "hub", "--remote"])

        self.assertIn("--remote", command)


class SetupTests(unittest.TestCase):
    def test_exported_values_win_over_the_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / ".env"
            path.write_text("# comment\nADMIN_EMAIL=file@example.org\nexport CLOUDFLARE_ACCOUNT_ID='abc'\n")

            values = setup.read_environment(path, {"ADMIN_EMAIL": "shell@example.org"})

        self.assertEqual(values["ADMIN_EMAIL"], "shell@example.org")
        self.assertEqual(values["CLOUDFLARE_ACCOUNT_ID"], "abc")

    def test_a_tunnel_is_named_for_its_machine(self) -> None:
        self.assertEqual(setup.default_tunnel_name("Derek's Box.local"), "practice-takes-hub-derek-s-box-local")

    def api(self, apps: list[dict], saved_aud: str = "aud-new"):
        calls: list[tuple] = []

        def request(method: str, path: str, body: dict | None) -> dict:
            calls.append((method, path, body))

            if path.endswith("/access/organizations"):
                return {"success": True, "result": {"auth_domain": "team.cloudflareaccess.com"}}

            if method == "GET":
                return {"success": True, "result": apps}

            return {"success": True, "result": {"aud": saved_aud}}

        return request, calls

    def test_the_access_application_is_created_once(self) -> None:
        request, calls = self.api([])

        team, audience = setup.ensure_access_application(request, "acct", HOSTNAME, "me@example.org")

        self.assertEqual((team, audience), ("team", "aud-new"))
        method, path, body = calls[-1]
        self.assertEqual((method, path), ("POST", "/accounts/acct/access/apps"))
        self.assertEqual(body["domain"], HOSTNAME)
        self.assertEqual(body["policies"][0]["include"], [{"email": {"email": "me@example.org"}}])
        self.assertFalse(body["app_launcher_visible"])

    def test_an_existing_application_is_updated(self) -> None:
        request, calls = self.api([{"id": "app1", "type": "self_hosted", "domain": HOSTNAME, "name": "x"}])

        setup.ensure_access_application(request, "acct", HOSTNAME, "me@example.org")

        self.assertEqual(calls[-1][:2], ("PUT", "/accounts/acct/access/apps/app1"))

    def test_duplicate_applications_stop_the_setup(self) -> None:
        request, _ = self.api(
            [
                {"id": "a", "type": "self_hosted", "domain": HOSTNAME},
                {"id": "b", "type": "self_hosted", "name": setup.APPLICATION_NAME},
            ]
        )

        with self.assertRaises(setup.SetupError):
            setup.ensure_access_application(request, "acct", HOSTNAME, "me@example.org")

    def test_a_refusal_from_cloudflare_is_said(self) -> None:
        def request(method: str, path: str, body: dict | None) -> dict:
            return {"success": False, "errors": [{"message": "Authentication error"}]}

        with self.assertRaises(setup.SetupError) as raised:
            setup.ensure_access_application(request, "acct", HOSTNAME, "me@example.org")

        self.assertIn("Authentication error", str(raised.exception))

    def test_an_existing_tunnel_is_reused(self) -> None:
        commands: list[list[str]] = []

        def run(command: list[str]) -> subprocess.CompletedProcess:
            commands.append(command)

            return completed(json.dumps([{"id": "t1", "name": "practice-takes-hub-box"}]))

        self.assertEqual(setup.ensure_tunnel("cloudflared", "practice-takes-hub-box", run), "t1")
        self.assertEqual(len(commands), 1)

    def test_a_missing_tunnel_is_created(self) -> None:
        replies = [completed("[]"), completed(json.dumps({"id": "t2"}))]

        def run(command: list[str]) -> subprocess.CompletedProcess:
            return replies.pop(0)

        self.assertEqual(setup.ensure_tunnel("cloudflared", "practice-takes-hub-box", run), "t2")

    def test_dns_is_never_overwritten(self) -> None:
        def run(command: list[str]) -> subprocess.CompletedProcess:
            self.assertNotIn("--overwrite-dns", command)

            return completed(returncode=1, stderr="record with that host already exists")

        with self.assertRaises(setup.SetupError) as raised:
            setup.route_dns("cloudflared", "t1", HOSTNAME, run)

        self.assertIn("does not overwrite", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
