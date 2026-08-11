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
    """Ids the page has: written in the markup, or built by the script itself.

    The script creates some — the canvases, the empty-state button — inside
    template strings, and those are just as real as the ones in the file.
    """
    from_markup = set(re.findall(r'id="([^"]+)"', markup()))
    from_script = set(re.findall(r'id="([^"$]+)"', script())) | set(
        # `element.id = "name"`, which is how the script names a node it built
        # rather than templated.
        re.findall(r'\.id = "([^"]+)"', script())
    )
    interpolated = {
        name.replace("${index}", "") for name in re.findall(r'id="([^"]*\$\{[^"]*)"', script())
    }

    return from_markup | from_script | interpolated


def stylesheet() -> str:
    return (WEB_ROOT / "style.css").read_text(encoding="utf-8")


def referenced_ids() -> set[str]:
    text = script()

    return set(re.findall(r'element\("([^"]+)"\)', text)) | set(
        re.findall(r'getElementById\("([^"]+)"\)', text)
    )


class PageWiringTests(unittest.TestCase):
    def test_every_id_the_script_uses_exists_in_the_markup(self) -> None:
        missing = {
            # The per-metric canvases are numbered at render time; the pair of
            # patterns cannot be matched literally, so they are checked by
            # test_every_canvas_the_script_draws_into_is_created_by_it instead.
            name for name in referenced_ids() - declared_ids()
            if not name.startswith("canvas-metric")
        }

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

    def test_the_filter_summary_is_set_rather_than_appended(self) -> None:
        """Appending is why it read "6 shown, 32 hidden" three times over."""
        self.assertNotIn("filter-summary", script().split("+=")[0][-200:])

        for line in script().splitlines():
            if "filtered.textContent" in line or "summary.textContent" in line:
                with self.subTest(line=line.strip()):
                    self.assertNotIn("+=", line)

    def test_changing_a_filter_redraws_the_filter_row_too(self) -> None:
        """Redrawing only the grid leaves the chips and the summary stale."""
        body = script()
        start = body.index("function applyFilters()")
        end = body.index("function cycleFilter")
        apply_body = body[start:end]

        self.assertIn("renderFilters(", apply_body)
        self.assertIn("renderGrid(", apply_body)

        # And every filter change goes through it.
        cycle = body[body.index("function cycleFilter"):body.index("function clearValue")]

        self.assertIn("applyFilters()", cycle)

    def test_an_emptied_facet_stops_being_a_filter(self) -> None:
        """A facet holding empty sets would match nothing and hide everything."""
        body = script()
        cycle = body[body.index("function cycleFilter"):body.index("function clearValue")]

        self.assertIn("delete state.filters[name]", cycle)

    def test_a_value_cycles_through_keep_exclude_and_off(self) -> None:
        body = script()
        cycle = body[body.index("function cycleFilter"):body.index("function clearValue")]

        # include -> exclude -> off, in that order.
        self.assertLess(cycle.index("include.has(value)"), cycle.index("exclude.has(value)"))
        self.assertIn("exclude.add(value)", cycle)
        self.assertIn("exclude.delete(value)", cycle)

    def test_excluding_beats_including(self) -> None:
        """Naming a value in both is a contradiction; dropping it is the safer reading."""
        body = script()
        matches = body[body.index("function matchesFilters"):body.index("// Everything that depends")]

        self.assertLess(matches.index("sets.exclude.has(value)"), matches.index("sets.include.size"))

    def test_the_facets_are_dropdowns(self) -> None:
        body = script()

        self.assertIn('createElement("details")', body)
        self.assertIn("facet-panel", body)

    def test_the_stop_key_reads_the_polled_job_not_the_last_reload(self) -> None:
        """`state.data` is only replaced by a full reload.

        Escape used to ask `state.data.job.running`, which during a run still
        held what was true before it started — not running — so the key did
        nothing for the entire run, which is exactly when it is wanted.
        """
        text = script()

        self.assertNotIn("state.data.job", text)
        self.assertIn("state.job = job", text)
        self.assertRegex(text, r'event\.key === "Escape" && state\.job')

    def test_nothing_hidden_is_laid_out_without_being_hideable(self) -> None:
        """Styling an element by id can stop `hidden` from hiding it.

        `[hidden] { display: none }` is a browser rule with no specificity to
        speak of, so `#thing { display: flex }` silently outranks it and the
        element is on the page forever. That shipped here once: a restart
        button sat above a hub that had nothing to restart, in an otherwise
        empty banner.
        """
        css = stylesheet()
        hideable = set(re.findall(r'id="([^"]+)"[^>]*\shidden', markup()))
        self.assertTrue(hideable)

        for name in sorted(hideable):
            laid_out = re.search(
                rf"#{re.escape(name)}\s*\{{[^}}]*\bdisplay\s*:", css
            )

            if not laid_out:
                continue

            with self.subTest(id=name):
                self.assertRegex(
                    css,
                    rf"#{re.escape(name)}\[hidden\]\s*\{{[^}}]*display\s*:\s*none",
                    f"#{name} is given a display by id but has no [hidden] rule, "
                    f"so hiding it in the script will not hide it",
                )

    def test_the_stylesheet_and_script_are_linked(self) -> None:
        self.assertIn('href="/web/style.css"', markup())
        self.assertIn('src="/web/app.js"', markup())

    def test_the_charting_library_is_vendored_and_loaded(self) -> None:
        """It ships with the tool so the graphs work with no network."""
        self.assertIn('src="/web/vendor/chart.umd.js"', markup())
        self.assertTrue((WEB_ROOT / "vendor" / "chart.umd.js").is_file())
        self.assertTrue((WEB_ROOT / "vendor" / "chart.js-LICENSE.md").is_file())

    def test_the_library_is_loaded_before_the_page_script(self) -> None:
        text = markup()

        self.assertLess(text.index("vendor/chart.umd.js"), text.index("/web/app.js"))

    def test_every_canvas_the_script_draws_into_is_created_by_it(self) -> None:
        """A canvas id that no markup or script creates is a chart that never appears."""
        script = globals()["script"]()
        drawn = set(re.findall(r'element\(`?canvas-([\w-]+?)(?:-\$\{index\})?`?\)', script))
        created = set(re.findall(r'id="canvas-([\w-]+?)(?:-\$\{index\})?"', script))

        self.assertTrue(drawn)
        self.assertEqual(drawn - created, set())


if __name__ == "__main__":
    unittest.main()
