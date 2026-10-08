#!/usr/bin/env python3
"""Tests for the manual score-import fixture generator.

These cover what Python can see. Which `MusicXmlImportStatus` a fixture
actually produces is the importer's answer, not this script's, and nothing here
can assert it -- see the generator's module docstring for why that gap is
deliberate and what to do when the importer changes.
"""

from __future__ import annotations

import os
import stat
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import make_score_import_fixtures as generator  # noqa: E402


class FixtureGeneration(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self._temporary.name) / "corpus"
        self.statuses = generator.generate(self.directory)

    def tearDown(self) -> None:
        # The mode-000 fixture would defeat TemporaryDirectory's cleanup.
        for path in self.directory.iterdir():
            if path.is_file() and not path.is_symlink():
                path.chmod(stat.S_IRUSR | stat.S_IWUSR)
        self._temporary.cleanup()

    def test_every_declared_fixture_is_written(self) -> None:
        for name in self.statuses:
            with self.subTest(name=name):
                # Not exists(): a dangling symlink must count as written.
                self.assertTrue((self.directory / name).is_symlink() or (self.directory / name).is_file())

    def test_every_written_file_is_declared(self) -> None:
        """No stray file a tester would not know what to do with."""
        written = {path.name for path in self.directory.iterdir()}
        self.assertEqual(written, set(self.statuses))

    def test_each_failure_status_is_covered(self) -> None:
        """Task 5.3 is only meaningful if no status is missing a file."""
        expected = {
            "imported",
            "importedWithDiagnostics",
            "notFound",
            "unreadable",
            "tooLarge",
            "notMusicXml",
            "malformedXml",
            "invalidContainer",
            "unsupportedDocumentType",
            "structurallyInvalid",
        }
        self.assertEqual(set(self.statuses.values()), expected)

    def test_the_not_found_fixture_dangles(self) -> None:
        link = self.directory / "01-not-found.musicxml"
        self.assertTrue(link.is_symlink())
        self.assertFalse(link.exists(), "the link must not resolve, or it imports fine")
        self.assertFalse(os.path.isabs(os.readlink(link)), "a relative target survives a move")

    def test_the_unreadable_fixture_cannot_be_read(self) -> None:
        path = self.directory / "02-unreadable.musicxml"
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0)
        if os.geteuid() == 0:
            self.skipTest("root ignores the permission bits")
        with self.assertRaises(PermissionError):
            path.read_text(encoding="utf-8")

    def test_the_ratio_bomb_exceeds_the_expansion_limit(self) -> None:
        path = self.directory / "03-too-large-ratio.mxl"
        with zipfile.ZipFile(path) as archive:
            entry = archive.getinfo("score.xml")
        ratio = entry.file_size / entry.compress_size
        self.assertGreater(ratio, generator.MAXIMUM_EXPANSION_RATIO)

    def test_the_large_score_stays_under_every_limit(self) -> None:
        """It must be slow to import, not refused -- a `tooLarge` here is a dud."""
        path = self.directory / "10-large.mxl"
        self.assertLess(path.stat().st_size, generator.MAXIMUM_SOURCE_BYTES)

        with zipfile.ZipFile(path) as archive:
            entry = archive.getinfo("score.xml")

        ratio = entry.file_size / entry.compress_size
        self.assertLess(
            ratio,
            generator.MAXIMUM_EXPANSION_RATIO,
            "the large score compressed past the ratio cap and would be refused",
        )
        # Big enough that the import is worth watching. The 48 MB this produces
        # took ~5.5 s on the machine it was written on.
        self.assertGreater(entry.file_size, 32 * 1024 * 1024)

    def test_the_lossy_fixture_carries_droppable_content(self) -> None:
        text = (self.directory / "09-lossy.musicxml").read_text(encoding="utf-8")
        for element in ("<harmony>", "<slur", "<articulations>", "<ornaments>", "<technical>"):
            with self.subTest(element=element):
                self.assertIn(element, text)
        self.assertIn("not-a-real-musicxml-element", text)

    def test_the_structurally_invalid_fixture_has_no_events(self) -> None:
        text = (self.directory / "08-structurally-invalid.musicxml").read_text(encoding="utf-8")
        self.assertIn("<measure", text)
        self.assertNotIn("<note>", text, "a note would make it import")
        self.assertNotIn("<rest/>", text, "a rest would make it import too")

    def test_generating_twice_in_a_row_succeeds(self) -> None:
        """The mode-000 fixture must not block the next run."""
        again = generator.generate(self.directory)
        self.assertEqual(again, self.statuses)


if __name__ == "__main__":
    unittest.main()
