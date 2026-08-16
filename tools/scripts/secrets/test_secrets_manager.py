"""Focused unit tests for the SOPS secrets manager's path safety and matching."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Iterator
import unittest
import unittest.mock


MODULE_PATH = Path(__file__).with_name("secrets_manager.py")
REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("secrets_manager", MODULE_PATH)
assert SPEC and SPEC.loader
secrets_manager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(secrets_manager)


@contextlib.contextmanager
def temporary_repository() -> Iterator[Path]:
    """Yield an empty Git repository so Git-backed helpers can be exercised."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        subprocess.run(
            ["git", "init", "--quiet", str(root)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        yield root


class PatternTests(unittest.TestCase):
    def test_double_star_matches_root_and_nested_files(self) -> None:
        self.assertTrue(secrets_manager.pattern_matches(".env", "**/.env"))
        self.assertTrue(
            secrets_manager.pattern_matches("src/services/api/.env", "**/.env")
        )

    def test_ordered_exclusion_wins(self) -> None:
        rules = [(False, "**/.env*"), (True, "**/.env.example")]
        self.assertTrue(secrets_manager.is_secret_path("api/.env.local", rules))
        self.assertFalse(secrets_manager.is_secret_path("api/.env.example", rules))

    def test_encrypted_mirrors_are_never_plaintext(self) -> None:
        rules = [(False, "**/.env.*")]
        self.assertFalse(
            secrets_manager.is_secret_path(".secrets/services/api/.env.sops", rules)
        )

    def test_mirror_mapping_round_trips(self) -> None:
        source = "src/services/feedback-intake/.dev.vars"
        mirror = secrets_manager.mirror_relative_path(source)
        self.assertEqual(
            mirror, ".secrets/src/services/feedback-intake/.dev.vars.sops"
        )
        self.assertEqual(secrets_manager.source_relative_path(mirror), source)

    def test_parent_traversal_is_rejected(self) -> None:
        with self.assertRaises(secrets_manager.SecretsError):
            secrets_manager.validate_relative_path("../outside.env")

    def test_patterns_containing_whitespace_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pattern_file = root / secrets_manager.PATTERN_FILE
            pattern_file.parent.mkdir(parents=True, exist_ok=True)
            pattern_file.write_text(
                "**/.dev.vars Plaintext secret files to mirror with SOPS.\n",
                encoding="utf-8",
            )
            with self.assertRaises(secrets_manager.SecretsError):
                secrets_manager.read_patterns(root)

    def test_discovery_obeys_exclusions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".env").write_text("TOKEN=secret\n", encoding="utf-8")
            (root / ".env.example").write_text("TOKEN=\n", encoding="utf-8")
            rules = [(False, ".env*"), (True, ".env.example")]
            found = secrets_manager.discover_plaintext(root, rules)
            self.assertEqual(list(found), [".env"])


class RepositoryPatternTests(unittest.TestCase):
    """Guard the live tools/secret-patterns file against silently dead rules."""

    def setUp(self) -> None:
        self.rules = secrets_manager.read_patterns(REPOSITORY_ROOT)

    def test_managed_plaintext_is_selected(self) -> None:
        for relative in (
            ".env",
            ".env.local",
            "src/services/feedback-intake/.env",
            "src/services/feedback-intake/.dev.vars",
            "secrets/cloudflare.env",
            "tools/deploy.secret",
        ):
            with self.subTest(relative=relative):
                self.assertTrue(
                    secrets_manager.is_secret_path(relative, self.rules)
                )

    def test_templates_and_sources_are_not_selected(self) -> None:
        for relative in (
            ".env.example",
            "src/services/feedback-intake/.env.example",
            "src/services/feedback-intake/.dev.vars.example",
            ".secrets/src/services/feedback-intake/.env.sops",
            "README.md",
            "src/Main.cpp",
        ):
            with self.subTest(relative=relative):
                self.assertFalse(
                    secrets_manager.is_secret_path(relative, self.rules)
                )

    def test_the_worker_configuration_is_not_a_secret(self) -> None:
        """wrangler.jsonc holds a D1 database_id and nothing else non-public.

        An identifier grants no access without separate credentials, so managing
        it as a secret bought a rotation obligation it could never discharge and
        left a template file free to drift from the real one. It is tracked now,
        with no example alongside it.
        """
        self.assertFalse(
            secrets_manager.is_secret_path(
                "src/services/feedback-intake/wrangler.jsonc", self.rules
            )
        )


class CommitGateTests(unittest.TestCase):
    """The pre-commit gate, which had no coverage before the vault migration.

    It is the only thing standing between a plaintext credential and the remote
    now that the encrypted mirrors are no longer committed, so its two jobs are
    pinned here: unstage a newly added secret, and refuse outright if one is
    already tracked.
    """

    def _repository(self, root: Path) -> None:
        (root / "tools").mkdir(parents=True, exist_ok=True)
        (root / "tools" / "secret-patterns").write_text(
            "**/.env\n!**/.env.example\n", encoding="utf-8"
        )
        for command in (
            ["git", "config", "user.email", "t@example.com"],
            ["git", "config", "user.name", "t"],
        ):
            subprocess.run(command, cwd=root, check=True)

    def _staged(self, root: Path) -> list[str]:
        out = subprocess.run(
            ["git", "diff", "--cached", "--name-only"],
            cwd=root, capture_output=True, text=True, check=True,
        )
        return sorted(p for p in out.stdout.split() if p)

    def test_a_newly_staged_plaintext_secret_is_unstaged(self) -> None:
        with temporary_repository() as root:
            self._repository(root)
            (root / ".env").write_text("TOKEN=live-value\n", encoding="utf-8")
            subprocess.run(["git", "add", "-f", ".env"], cwd=root, check=True)
            self.assertIn(".env", self._staged(root))

            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(secrets_manager.protect_command(root), 0)

            self.assertNotIn(".env", self._staged(root))
            # Unstaged, not deleted -- the developer still needs the file.
            self.assertTrue((root / ".env").is_file())

    def test_an_already_tracked_plaintext_secret_is_refused(self) -> None:
        """Unstaging cannot help here -- the secret is already in history.

        The gate refuses rather than silently unstaging, because quietly
        dropping the change would leave the committed credential in place while
        looking like it had been dealt with.
        """
        with temporary_repository() as root:
            self._repository(root)
            (root / ".env").write_text("TOKEN=live-value\n", encoding="utf-8")
            subprocess.run(["git", "add", "-f", ".env"], cwd=root, check=True)
            subprocess.run(
                ["git", "commit", "--quiet", "-m", "oops"], cwd=root, check=True
            )
            # The gate reads `git diff --cached`, so a tracked secret only
            # reaches it when it is being changed. An unmodified tracked secret
            # is the `audit` command's job, not this one.
            (root / ".env").write_text("TOKEN=rotated\n", encoding="utf-8")
            subprocess.run(["git", "add", "-f", ".env"], cwd=root, check=True)

            with self.assertRaises(secrets_manager.SecretsError):
                with contextlib.redirect_stdout(io.StringIO()):
                    secrets_manager.protect_command(root)

    def test_audit_catches_a_tracked_secret_the_gate_would_not_see(self) -> None:
        """The complement: committed and unmodified, so no staged diff exists."""
        with temporary_repository() as root:
            self._repository(root)
            (root / ".env").write_text("TOKEN=live-value\n", encoding="utf-8")
            subprocess.run(["git", "add", "-f", ".env"], cwd=root, check=True)
            subprocess.run(
                ["git", "commit", "--quiet", "-m", "oops"], cwd=root, check=True
            )

            with contextlib.redirect_stdout(io.StringIO()):
                secrets_manager.protect_command(root)  # sees nothing staged

            with self.assertRaises(secrets_manager.SecretsError):
                with contextlib.redirect_stdout(io.StringIO()):
                    secrets_manager.audit_command(root)

    def test_the_gate_no_longer_stages_encrypted_mirrors(self) -> None:
        """The narrowing: mirrors are untracked, so nothing should be staged."""
        with temporary_repository() as root:
            self._repository(root)
            (root / ".env").write_text("TOKEN=live-value\n", encoding="utf-8")

            with contextlib.redirect_stdout(io.StringIO()):
                secrets_manager.protect_command(root)

            self.assertEqual(self._staged(root), [])
            self.assertFalse((root / ".secrets").exists())

    def test_sync_does_not_stage_ignored_mirrors(self) -> None:
        """`encrypt` stopped staging mirrors; `sync` kept doing it and failed.

        `git add` on a path inside an ignored `.secrets/` exits non-zero, and the
        raise happened before `save_state`, so the run left mirrors on disk with
        no sync baseline recorded for them.
        """
        with temporary_repository() as root:
            self._repository(root)
            (root / ".gitignore").write_text("/.secrets/\n", encoding="utf-8")
            (root / ".env").write_text("TOKEN=live-value\n", encoding="utf-8")

            with unittest.mock.patch.object(
                secrets_manager, "encrypt_bytes",
                lambda _root, _relative, plaintext: b"ENC:" + plaintext,
            ):
                with contextlib.redirect_stdout(io.StringIO()):
                    secrets_manager.sync_command(root, prefer=None)

            self.assertEqual(self._staged(root), [])
            self.assertTrue((root / ".secrets" / ".env.sops").is_file())
            # save_state runs only if nothing raised before it.
            state = json.loads(
                secrets_manager.state_path(root).read_text(encoding="utf-8")
            )
            self.assertIn(".env", state)

    def test_a_staged_non_secret_is_left_alone(self) -> None:
        with temporary_repository() as root:
            self._repository(root)
            (root / "README.md").write_text("hello\n", encoding="utf-8")
            subprocess.run(["git", "add", "README.md"], cwd=root, check=True)

            with contextlib.redirect_stdout(io.StringIO()):
                secrets_manager.protect_command(root)

            self.assertIn("README.md", self._staged(root))


class InitTests(unittest.TestCase):
    def test_init_writes_a_regex_that_matches_mirror_paths(self) -> None:
        recipient = "age1" + "0" * 55
        with temporary_repository() as root:
            with contextlib.redirect_stdout(io.StringIO()):
                secrets_manager.init_command(root, recipient)
            self.assertTrue((root / secrets_manager.PATTERN_FILE).is_file())
            content = (root / ".sops.yaml").read_text(encoding="utf-8")
            match = re.search(r"^\s*- path_regex: (.+)$", content, re.MULTILINE)
            self.assertIsNotNone(match)
            assert match is not None
            pattern = match.group(1)
            self.assertRegex(".secrets/services/api/.env.sops", pattern)
            self.assertNotRegex("src/services/api/.env", pattern)


class SyncConflictTests(unittest.TestCase):
    def test_conflict_copies_are_removed_once_resolved(self) -> None:
        relative = "src/services/api/.env"
        with temporary_repository() as root:
            secrets_manager.record_sync_conflict(
                root, relative, b"TOKEN=local\n", b"TOKEN=encrypted\n"
            )
            copies = secrets_manager.conflict_comparison_paths(root, relative)
            for copy in copies:
                self.assertTrue(copy.is_file())
                self.assertEqual(copy.stat().st_mode & 0o777, 0o600)
            secrets_manager.clear_sync_conflict(root, relative)
            for copy in copies:
                self.assertFalse(copy.exists())


if __name__ == "__main__":
    unittest.main()
