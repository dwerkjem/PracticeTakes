#!/usr/bin/env python3
"""Tests for when "Build desktop packages" may skip its build.

The failure that matters is the build skipping a change it should have built:
the three platform checks are required, a skipped step still reports success,
and a broken change would merge green. So most of these pin that what the build
reads is included, and only then that documentation-only changes are not.
"""

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from changed_paths import applies, is_included, parse_patterns  # noqa: E402

PATTERNS = parse_patterns((Path(__file__).resolve().parent / "desktop_build_paths.txt").read_text(encoding="utf-8"))


class BuildsWhenTheBuildCouldChange(unittest.TestCase):
    def test_every_build_input_builds(self):
        for path in (
            "src/application/shell/MainComponent.cpp",
            "src/platform/audio/AudioInputService.h",
            "src/tests/platform/score/TempoMapTests.cpp",
            "src/tests/resources/scores/sample.mxl",
            "src/services/feedback-intake/src/index.ts",
            "CMakeLists.txt",
            "tools/cmake/Sources.cmake",
            "tools/cmake/Packaging.cmake",
            "tools/packaging/icons/app.png",
            "tools/packaging/fonts/Bravura.otf",
            "tools/scripts/build/build-and-run.sh",
            "tools/VERSION",
            "tools/vcpkg.json",
            ".clang-format",
            "pyproject.toml",
        ):
            with self.subTest(path=path):
                self.assertTrue(is_included(path, PATTERNS))

    def test_the_readme_builds_because_the_packages_ship_it(self):
        self.assertTrue(is_included("README.md", PATTERNS))

    def test_contracts_build_because_the_test_build_writes_one(self):
        self.assertTrue(is_included("docs/contracts/testcontrol/approved-states.json", PATTERNS))
        self.assertTrue(is_included("docs/contracts/feedback/v1.schema.json", PATTERNS))

    def test_the_workflows_that_run_the_build_build(self):
        self.assertTrue(is_included(".github/workflows/build-multiplatform.yml", PATTERNS))
        self.assertTrue(is_included(".github/workflows/release.yml", PATTERNS))

    def test_a_file_nobody_listed_builds(self):
        # The default is to build. Something new at an unforeseen path must not
        # be the thing that slips through unbuilt.
        self.assertTrue(is_included("somewhere/new/file.xyz", PATTERNS))
        self.assertTrue(is_included(".github/actions/new-action/action.yml", PATTERNS))

    def test_one_build_input_among_documentation_builds(self):
        self.assertTrue(applies(["docs/wiki/Home.md", "src/bootstrap/Main.cpp"], PATTERNS))


class SkipsWhenOnlyDocumentationChanged(unittest.TestCase):
    def test_documentation_only_changes_skip(self):
        for path in (
            "docs/wiki/Home.md",
            "docs/development/architecture/ARCHITECTURE.md",
            "docs/development/quality/manual-runs/0.6.0.json",
            "openspec/changes/internationalization/tasks.md",
            "openspec/specs/grand-staff/spec.md",
            "tools/scripts/README.md",
            "src/tests/resources/README.md",
            "CLAUDE.md",
            ".github/CONTRIBUTING.md",
            ".github/PULL_REQUEST_TEMPLATE.md",
            ".github/ISSUE_TEMPLATE/bug.yml",
            ".github/workflows/wiki.yml",
            ".github/workflows/roadmap-forecast.yml",
        ):
            with self.subTest(path=path):
                self.assertFalse(is_included(path, PATTERNS))

    def test_the_wiki_pull_request_would_have_skipped(self):
        # #289, the change that prompted this: wiki pages, one workflow, two
        # documentation edits.
        changed = [
            ".github/workflows/wiki.yml",
            "docs/development/README.md",
            "docs/development/agents/AGENT_GUIDE.md",
            "docs/wiki/Home.md",
            "docs/wiki/_Sidebar.md",
        ]
        self.assertFalse(applies(changed, PATTERNS))


if __name__ == "__main__":
    unittest.main()
