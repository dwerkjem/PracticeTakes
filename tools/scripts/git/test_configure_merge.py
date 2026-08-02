"""Unit tests for the merge-configuration installer.

Each test drives a throwaway repository so nothing here can disturb the
developer's own Git config.
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
from typing import Iterator
import unittest


MODULE_PATH = Path(__file__).with_name("configure_merge.py")
SPEC = importlib.util.spec_from_file_location("configure_merge", MODULE_PATH)
assert SPEC and SPEC.loader
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


@contextlib.contextmanager
def repository() -> Iterator[Path]:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        subprocess.run(["git", "init", "--quiet", str(root)], check=True)
        yield root


def value(root: Path, key: str) -> str | None:
    completed = subprocess.run(
        ["git", "-C", str(root), "config", "--local", "--get", key],
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip() if completed.returncode == 0 else None


def run(root: Path, *arguments: str) -> int:
    with contextlib.redirect_stdout(io.StringIO()):
        return installer.main([*arguments, "--repository", str(root)])


class InstallTests(unittest.TestCase):
    def test_a_fresh_clone_gets_every_setting(self) -> None:
        with repository() as root:
            self.assertEqual(run(root), 0)

            self.assertEqual(value(root, "merge.conflictStyle"), "zdiff3")
            self.assertEqual(value(root, "merge.directoryRenames"), "true")
            self.assertEqual(value(root, "rerere.enabled"), "true")
            self.assertEqual(value(root, "merge.ours.driver"), "true")

    def test_the_rename_limits_are_raised_together(self) -> None:
        """A raised merge limit is useless if diff still gives up first."""
        with repository() as root:
            run(root)
            self.assertEqual(
                value(root, "merge.renameLimit"), value(root, "diff.renameLimit")
            )
            self.assertGreater(int(value(root, "merge.renameLimit") or 0), 1000)

    def test_the_custom_drivers_are_registered(self) -> None:
        with repository() as root:
            run(root)

            for name, script in (
                ("cmake-sources", "merge_cmake_sources.py"),
                ("npm-lock", "merge_npm_lock.py"),
            ):
                command = value(root, f"merge.{name}.driver")
                self.assertIsNotNone(command, f"{name} driver not registered")
                assert command is not None
                self.assertIn(script, command)
                self.assertIn("%O %A %B %P", command)
                self.assertIsNotNone(value(root, f"merge.{name}.name"))

    def test_the_driver_command_is_relative_not_absolute(self) -> None:
        """Linked worktrees share one .git/config.

        An absolute path here gets rewritten to whichever worktree configured
        last, and every merge in every worktree breaks as soon as that
        directory is removed. Git runs merge drivers from the top of the
        working tree, so a relative path is correct everywhere at once.
        """
        for name, _, _ in installer.DRIVERS:
            command = dict(installer.desired_settings(Path("/any/checkout")))[
                f"merge.{name}.driver"
            ]
            self.assertNotIn("/any/checkout", command)
            self.assertIn("tools/scripts/git/", command)
            self.assertNotRegex(command, r"python3 /")

    def test_the_command_does_not_depend_on_the_checkout(self) -> None:
        first = dict(installer.desired_settings(Path("/one")))
        second = dict(installer.desired_settings(Path("/two")))
        self.assertEqual(first, second)

    def test_every_registered_driver_script_exists(self) -> None:
        """A driver named in config but missing on disk breaks every merge."""
        for _, script, _ in installer.DRIVERS:
            path = installer.REPOSITORY_ROOT / installer.DRIVER_DIRECTORY / script
            self.assertTrue(path.is_file(), f"{path} is missing")

    def test_running_twice_changes_nothing_the_second_time(self) -> None:
        with repository() as root:
            run(root)
            self.assertEqual(installer.apply(root, check_only=True), [])


class CheckModeTests(unittest.TestCase):
    def test_check_reports_a_missing_setting_without_writing(self) -> None:
        with repository() as root:
            self.assertEqual(run(root, "--check"), 1)
            self.assertIsNone(value(root, "merge.conflictStyle"))

    def test_check_passes_once_configured(self) -> None:
        with repository() as root:
            run(root)
            self.assertEqual(run(root, "--check"), 0)

    def test_a_stale_value_is_detected_and_corrected(self) -> None:
        with repository() as root:
            run(root)
            subprocess.run(
                ["git", "-C", str(root), "config", "--local", "merge.conflictStyle", "merge"],
                check=True,
            )

            self.assertEqual(run(root, "--check"), 1)
            self.assertEqual(run(root), 0)
            self.assertEqual(value(root, "merge.conflictStyle"), "zdiff3")


class GuardTests(unittest.TestCase):
    def test_a_directory_that_is_not_a_repository_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with contextlib.redirect_stderr(io.StringIO()):
                code = installer.main(["--repository", temporary])

        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
