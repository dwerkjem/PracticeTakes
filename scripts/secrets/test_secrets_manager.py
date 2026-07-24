"""Focused unit tests for the SOPS secrets manager's path safety and matching."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).with_name("secrets_manager.py")
SPEC = importlib.util.spec_from_file_location("secrets_manager", MODULE_PATH)
assert SPEC and SPEC.loader
secrets_manager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(secrets_manager)


class PatternTests(unittest.TestCase):
    def test_double_star_matches_root_and_nested_files(self) -> None:
        self.assertTrue(secrets_manager.pattern_matches(".env", "**/.env"))
        self.assertTrue(
            secrets_manager.pattern_matches("services/api/.env", "**/.env")
        )

    def test_ordered_exclusion_wins(self) -> None:
        rules = [(False, "**/.env*"), (True, "**/.env.example")]
        self.assertTrue(secrets_manager.is_secret_path("api/.env.local", rules))
        self.assertFalse(secrets_manager.is_secret_path("api/.env.example", rules))

    def test_mirror_mapping_round_trips(self) -> None:
        source = "services/feedback-intake/.dev.vars"
        mirror = secrets_manager.mirror_relative_path(source)
        self.assertEqual(
            mirror, ".secrets/services/feedback-intake/.dev.vars.sops"
        )
        self.assertEqual(secrets_manager.source_relative_path(mirror), source)

    def test_parent_traversal_is_rejected(self) -> None:
        with self.assertRaises(secrets_manager.SecretsError):
            secrets_manager.validate_relative_path("../outside.env")

    def test_discovery_obeys_exclusions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / ".env").write_text("TOKEN=secret\n", encoding="utf-8")
            (root / ".env.example").write_text("TOKEN=\n", encoding="utf-8")
            rules = [(False, ".env*"), (True, ".env.example")]
            found = secrets_manager.discover_plaintext(root, rules)
            self.assertEqual(list(found), [".env"])


class PlaintextMergeTests(unittest.TestCase):
    def test_non_overlapping_secret_edits_merge_cleanly(self) -> None:
        merged, clean = secrets_manager.merge_plaintext(
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
        merged, clean = secrets_manager.merge_plaintext(
            b"TOKEN=ours\n",
            b"TOKEN=base\n",
            b"TOKEN=theirs\n",
        )
        self.assertFalse(clean)
        self.assertIn(b"<<<<<<<", merged)
        self.assertIn(b">>>>>>>", merged)


if __name__ == "__main__":
    unittest.main()
