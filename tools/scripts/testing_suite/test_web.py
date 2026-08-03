#!/usr/bin/env python3
"""Static checks on the hub page.

The browser layer is the one part of the suite no test drives, by design — every
decision it could get wrong lives server-side. What remains is wiring: an id the
script reaches for that the markup does not define is a silently dead button, and
that is exactly the failure a machine with no browser installed cannot notice.

So this reads both files and checks they agree. Cheap, and it catches the
mistake that actually happens.
"""

from __future__ import annotations

from pathlib import Path
import re
import unittest

WEB_ROOT = Path(__file__).resolve().parent / "web"


def markup() -> str:
    return (WEB_ROOT / "index.html").read_text(encoding="utf-8")


def script() -> str:
    return (WEB_ROOT / "app.js").read_text(encoding="utf-8")


def declared_ids() -> set[str]:
    return set(re.findall(r'id="([^"]+)"', markup()))


def referenced_ids() -> set[str]:
    text = script()

    return set(re.findall(r'element\("([^"]+)"\)', text)) | set(
        re.findall(r'getElementById\("([^"]+)"\)', text)
    )


class PageWiringTests(unittest.TestCase):
    def test_every_id_the_script_uses_exists_in_the_markup(self) -> None:
        # `capture-now` is created by the script itself for the empty state, so
        # it is the one id that legitimately is not in the file.
        missing = referenced_ids() - declared_ids() - {"capture-now"}

        self.assertEqual(missing, set(), f"the script reaches for ids that do not exist: {missing}")

    def test_the_three_views_exist(self) -> None:
        for view in ("view-run", "view-review", "view-results"):
            with self.subTest(view=view):
                self.assertIn(f'id="{view}"', markup())

    def test_every_tab_names_a_view(self) -> None:
        tabs = set(re.findall(r'data-view="([^"]+)"', markup()))

        for tab in tabs:
            with self.subTest(tab=tab):
                self.assertIn(f'id="view-{tab}"', markup())

    def test_the_run_view_is_what_opens_first(self) -> None:
        """An empty store is the first thing anybody sees; it needs a way forward."""
        self.assertIn('<main id="view-run" class="view">', markup())
        self.assertIn('id="view-review" class="view" hidden', markup())

    def test_the_endpoints_the_script_calls_are_served(self) -> None:
        import sys

        sys.path.insert(0, str(Path(__file__).resolve().parent))

        import server  # noqa: PLC0415 - imported here so the file stays readable standalone

        routes = server.__file__ and Path(server.__file__).read_text(encoding="utf-8")
        called = set(re.findall(r'api\("(/api/[^"?]+)"', script()))

        for path in called:
            with self.subTest(path=path):
                self.assertIn(f'"{path}"', routes)

    def test_the_stylesheet_and_script_are_linked(self) -> None:
        self.assertIn('href="/web/style.css"', markup())
        self.assertIn('src="/web/app.js"', markup())


if __name__ == "__main__":
    unittest.main()
