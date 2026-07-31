"""Unit tests for the release version tooling the release workflow depends on."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
from typing import Iterator
import unittest
from unittest import mock

import contextlib


MODULE_PATH = Path(__file__).with_name("version.py")
SPEC = importlib.util.spec_from_file_location("release_version", MODULE_PATH)
assert SPEC and SPEC.loader
version = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(version)


@contextlib.contextmanager
def temporary_project(current: str = "1.2.3\n") -> Iterator[tuple[Path, Path]]:
    """Yield throwaway VERSION and vcpkg.json files bound to the module."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        version_file = root / "VERSION"
        manifest_file = root / "vcpkg.json"
        version_file.write_text(current, encoding="utf-8")
        manifest_file.write_text(
            json.dumps({"name": "practice-takes", "version-string": "0.0.0"}) + "\n",
            encoding="utf-8",
        )
        with mock.patch.object(version, "VERSION_FILE", version_file), \
             mock.patch.object(version, "VCPKG_MANIFEST_FILE", manifest_file):
            yield version_file, manifest_file


class ParseVersionTests(unittest.TestCase):
    def test_parses_major_minor_patch(self) -> None:
        self.assertEqual(version.parse_version("1.2.3"), (1, 2, 3))

    def test_ignores_surrounding_whitespace(self) -> None:
        self.assertEqual(version.parse_version(" 10.0.4\n"), (10, 0, 4))

    def test_accepts_zero_components(self) -> None:
        self.assertEqual(version.parse_version("0.0.0"), (0, 0, 0))

    def test_rejects_malformed_versions(self) -> None:
        for value in (
            "1.2",
            "1.2.3.4",
            "v1.2.3",
            "1.2.3-rc.1",
            "01.2.3",
            "1.02.3",
            "1.2.x",
            "",
            "not-a-version",
        ):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    version.parse_version(value)


class FormatVersionTests(unittest.TestCase):
    def test_formats_a_parsed_tuple(self) -> None:
        self.assertEqual(version.format_version((2, 11, 0)), "2.11.0")

    def test_round_trips_through_parsing(self) -> None:
        self.assertEqual(
            version.format_version(version.parse_version("4.5.6")), "4.5.6"
        )


class CalculateNextTests(unittest.TestCase):
    def test_patch_increments_only_the_patch(self) -> None:
        self.assertEqual(version.calculate_next((1, 2, 3), "patch"), (1, 2, 4))

    def test_minor_resets_the_patch(self) -> None:
        self.assertEqual(version.calculate_next((1, 2, 3), "minor"), (1, 3, 0))

    def test_major_resets_the_minor_and_patch(self) -> None:
        self.assertEqual(version.calculate_next((1, 2, 3), "major"), (2, 0, 0))

    def test_rejects_an_unsupported_bump(self) -> None:
        with self.assertRaises(ValueError):
            version.calculate_next((1, 2, 3), "epoch")


class VersionFileTests(unittest.TestCase):
    def test_reads_the_current_version(self) -> None:
        with temporary_project("3.4.5\n"):
            self.assertEqual(version.read_version(), (3, 4, 5))

    def test_reports_a_missing_version_file(self) -> None:
        with temporary_project() as (version_file, _):
            version_file.unlink()
            with self.assertRaises(ValueError):
                version.read_version()

    def test_rejects_a_corrupt_version_file(self) -> None:
        with temporary_project("not-a-version\n") as (_, _manifest):
            with self.assertRaises(ValueError):
                version.read_version()

    def test_writes_the_version_to_both_files(self) -> None:
        with temporary_project() as (version_file, manifest_file):
            self.assertEqual(version.write_version((2, 0, 0)), "2.0.0")
            self.assertEqual(version_file.read_text(encoding="utf-8"), "2.0.0\n")
            manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
            self.assertEqual(manifest["version-string"], "2.0.0")
            self.assertEqual(manifest["name"], "practice-takes")

    def test_bumping_round_trips_through_the_version_file(self) -> None:
        with temporary_project("1.2.3\n"):
            version.write_version(version.calculate_next(version.read_version(), "minor"))
            self.assertEqual(version.read_version(), (1, 3, 0))

    def test_leaves_the_repository_version_untouched(self) -> None:
        repository_version = version.PROJECT_ROOT / "VERSION"
        before = repository_version.read_text(encoding="utf-8")
        with temporary_project():
            version.write_version((9, 9, 9))
        self.assertEqual(repository_version.read_text(encoding="utf-8"), before)


if __name__ == "__main__":
    unittest.main()
