"""Focused unit tests for the SOPS secrets manager's path safety and matching."""

from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Iterator
import unittest


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
            "src/services/feedback-intake/wrangler.jsonc",
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
            "src/services/feedback-intake/wrangler.example.jsonc",
            ".secrets/src/services/feedback-intake/.env.sops",
            "README.md",
            "src/Main.cpp",
        ):
            with self.subTest(relative=relative):
                self.assertFalse(
                    secrets_manager.is_secret_path(relative, self.rules)
                )


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


class PlaintextMergeTests(unittest.TestCase):
    def test_non_overlapping_secret_edits_merge_cleanly(self) -> None:
        with temporary_repository() as root:
            merged, clean = secrets_manager.merge_plaintext(
                root,
                b"FIRST=ours\nKEEP_A=base\nKEEP_B=base\nSECOND=base\n",
                b"FIRST=base\nKEEP_A=base\nKEEP_B=base\nSECOND=base\n",
                b"FIRST=base\nKEEP_A=base\nKEEP_B=base\nSECOND=theirs\n",
            )
        self.assertTrue(clean)
        self.assertEqual(
            merged,
            b"FIRST=ours\nKEEP_A=base\nKEEP_B=base\nSECOND=theirs\n",
        )

    def test_overlapping_secret_edits_produce_markers(self) -> None:
        with temporary_repository() as root:
            merged, clean = secrets_manager.merge_plaintext(
                root,
                b"TOKEN=ours\n",
                b"TOKEN=base\n",
                b"TOKEN=theirs\n",
            )
        self.assertFalse(clean)
        self.assertIn(b"<<<<<<<", merged)
        self.assertIn(b">>>>>>>", merged)

    def test_scratch_plaintext_stays_inside_the_git_directory(self) -> None:
        with temporary_repository() as root:
            secrets_manager.merge_plaintext(root, b"A=1\n", b"A=1\n", b"A=1\n")
            scratch_root = (
                secrets_manager.git_directory(root)
                / secrets_manager.CONFLICT_DIRECTORY
            )
            self.assertEqual(list(scratch_root.glob("merge-*")), [])


if __name__ == "__main__":
    unittest.main()
