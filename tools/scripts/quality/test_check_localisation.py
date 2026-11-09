#!/usr/bin/env python3
"""Tests for the catalogue-against-code check."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_localisation import find_problems, without_comments  # noqa: E402


class CheckLocalisationTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        root = Path(self._temporary.name)
        self.source = root / "src"
        self.catalogues = root / "localisation"
        self.source.mkdir()
        self.catalogues.mkdir()
        self.addCleanup(self._temporary.cleanup)

    def write_source(self, relative: str, text: str) -> None:
        path = self.source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def write_catalogue(self, name: str, entries: list[str], language: str = "English") -> None:
        lines = ["# A header comment.", f"language: {language}", *entries]
        (self.catalogues / name).write_text("\n".join(lines) + "\n", encoding="utf-8")

    def problems(self) -> list[str]:
        return find_problems(self.source, self.catalogues)

    def test_agreement_has_no_problems(self) -> None:
        self.write_catalogue("en.txt", ['"tuner.title" = "Tuner"'])
        self.write_source("features/Tuner.cpp", 'auto t = l10n::text("tuner.title");\n')

        self.assertEqual(self.problems(), [])

    def test_a_reference_english_lacks_is_a_problem(self) -> None:
        """Otherwise a user reads the identifier where a sentence should be."""
        self.write_catalogue("en.txt", [])
        self.write_source("features/Tuner.cpp", 'auto t = l10n::text("tuner.title");\n')

        problems = self.problems()

        self.assertEqual(len(problems), 1)
        self.assertIn("tuner.title", problems[0])
        self.assertIn("Tuner.cpp:1", problems[0])

    def test_an_entry_nothing_uses_is_a_problem(self) -> None:
        self.write_catalogue("en.txt", ['"tuner.title" = "Tuner"', '"gone" = "Gone"'])
        self.write_source("features/Tuner.cpp", 'auto t = l10n::text("tuner.title");\n')

        problems = self.problems()

        self.assertEqual(len(problems), 1)
        self.assertIn("'gone'", problems[0])
        self.assertIn("en.txt:4", problems[0])

    def test_a_key_used_only_in_a_comment_is_unused(self) -> None:
        """The case that matters: a comment mentioning a key does not keep it alive."""
        self.write_catalogue("en.txt", ['"a" = "A"', '"b" = "B"'])
        self.write_source(
            "features/Tool.cpp",
            '// l10n::text("a") used to be here\n/* and l10n::text("b")\n   here */\nint x;\n',
        )

        problems = self.problems()

        self.assertEqual(len(problems), 2)
        self.assertTrue(all("nothing in src/ uses" in problem for problem in problems))

    def test_a_key_used_only_in_a_test_is_unused(self) -> None:
        self.write_catalogue("en.txt", ['"a" = "A"'])
        self.write_source("tests/features/ToolTests.cpp", 'CHECK(l10n::text("a") == "A");\n')

        problems = self.problems()

        self.assertEqual(len(problems), 1)
        self.assertIn("nothing in src/ uses", problems[0])

    def test_a_test_may_use_keys_english_lacks(self) -> None:
        self.write_catalogue("en.txt", [])
        self.write_source("tests/application/LocalisationTests.cpp", 'l10n::text("made.up");\n')

        self.assertEqual(self.problems(), [])

    def test_slashes_inside_a_string_do_not_start_a_comment(self) -> None:
        self.write_catalogue("en.txt", ['"a" = "A"'])
        self.write_source("features/Tool.cpp", 'auto u = "https://example"; auto t = l10n::text("a");\n')

        self.assertEqual(self.problems(), [])

    def test_a_marked_table_entry_counts(self) -> None:
        self.write_catalogue("en.txt", ['"settings.theme" = "Theme"'])
        self.write_source(
            "application/Settings.cpp",
            'constexpr std::string_view names[] = {l10n::key("settings.theme")};\n',
        )

        self.assertEqual(self.problems(), [])

    def test_a_plural_uses_every_form_and_needs_one_and_other(self) -> None:
        self.write_catalogue("en.txt", ['"bars.one" = "{count} bar"', '"bars.other" = "{count} bars"'])
        self.write_source("features/Score.cpp", 'l10n::plural("bars", count);\n')

        self.assertEqual(self.problems(), [])

        self.write_catalogue("en.txt", ['"bars.other" = "{count} bars"'])

        problems = self.problems()

        self.assertEqual(len(problems), 1)
        self.assertIn("bars.one", problems[0])

    def test_a_plural_form_is_not_a_plain_reference(self) -> None:
        self.write_catalogue("en.txt", ['"bars.one" = "{count} bar"', '"bars.other" = "{count} bars"'])
        self.write_source("features/Score.cpp", 'l10n::text("bars");\n')

        problems = self.problems()

        self.assertTrue(any("'bars'" in problem and "does not define" in problem for problem in problems))

    def test_a_translation_may_add_plural_forms_english_lacks(self) -> None:
        self.write_catalogue("en.txt", ['"bars.one" = "{count} bar"', '"bars.other" = "{count} bars"'])
        self.write_catalogue("pl.txt", ['"bars.few" = "{count} takty"'], language="Polski")
        self.write_source("features/Score.cpp", 'l10n::plural("bars", count);\n')

        self.assertEqual(self.problems(), [])

    def test_a_translation_of_nothing_is_a_problem(self) -> None:
        self.write_catalogue("en.txt", ['"a" = "A"'])
        self.write_catalogue("de.txt", ['"a" = "A"', '"stale" = "Alt"'], language="Deutsch")
        self.write_source("features/Tool.cpp", 'l10n::text("a");\n')

        problems = self.problems()

        self.assertEqual(len(problems), 1)
        self.assertIn("de.txt", problems[0])
        self.assertIn("'stale'", problems[0])

    def test_a_key_defined_twice_is_a_problem(self) -> None:
        """JUCE keeps the last silently, so the first is dead text nobody notices."""
        self.write_catalogue("en.txt", ['"a" = "A"', '"a" = "Again"'])
        self.write_source("features/Tool.cpp", 'l10n::text("a");\n')

        problems = self.problems()

        self.assertEqual(len(problems), 1)
        self.assertIn("a second time", problems[0])

    def test_testcontrol_is_never_localised(self) -> None:
        """The capture harness matches on those strings."""
        self.write_catalogue("en.txt", ['"a" = "A"'])
        self.write_source("features/Tool.cpp", 'l10n::text("a");\n')
        self.write_source(
            "application/testcontrol/States.cpp",
            '#include "application/localisation/Localisation.h"\n',
        )
        self.write_source("application/testcontrol/Other.cpp", 'auto d = l10n::text("a");\n')
        self.write_source("application/testcontrol/Fine.cpp", '// l10n::text is not used here\n')

        problems = self.problems()

        self.assertEqual(len(problems), 2)
        self.assertTrue(all("capture harness" in problem for problem in problems))

    def test_a_missing_english_catalogue_is_a_problem(self) -> None:
        problems = self.problems()

        self.assertEqual(len(problems), 1)
        self.assertIn("No English catalogue", problems[0])


class WithoutCommentsTests(unittest.TestCase):
    def test_comments_are_blanked_and_lines_kept(self) -> None:
        text = 'a // b\n/* c\nd */ e "// f" R"x(/* g */)x"'

        stripped = without_comments(text)

        self.assertEqual(stripped.count("\n"), text.count("\n"))
        self.assertNotIn("b", stripped)
        self.assertNotIn("c", stripped)
        self.assertIn('"// f"', stripped)
        self.assertIn('R"x(/* g */)x"', stripped)

    def test_an_escaped_quote_does_not_end_a_string(self) -> None:
        stripped = without_comments('"a \\" // b" c')

        self.assertIn("// b", stripped)


if __name__ == "__main__":
    unittest.main()
