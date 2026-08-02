"""Unit tests for the npm lockfile merge driver.

The driver shells out to npm, so these tests put a stub npm on PATH. What is
being pinned is the refusal behaviour: a clone without npm, or with a manifest
that is itself conflicted, must produce an ordinary Git conflict rather than a
lockfile that matches neither branch.
"""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import tempfile
from typing import Iterator
import unittest

import contextlib


MODULE_PATH = Path(__file__).with_name("merge_npm_lock.py")
SPEC = importlib.util.spec_from_file_location("merge_npm_lock", MODULE_PATH)
assert SPEC and SPEC.loader
driver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(driver)


LOCK = {"name": "pkg", "lockfileVersion": 3, "packages": {}}


@contextlib.contextmanager
def workspace(manifest_text: str = '{"name": "pkg"}') -> Iterator[Path]:
    """A repository-shaped directory with a manifest in src/services/."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        directory = root / "src" / "services"
        directory.mkdir(parents=True)
        (directory / "package.json").write_text(manifest_text, encoding="utf-8")
        yield root


@contextlib.contextmanager
def stub_npm(root: Path, *, succeeds: bool, writes: str | None = None) -> Iterator[None]:
    """Put a fake npm first on PATH."""
    bin_directory = root / "stub-bin"
    bin_directory.mkdir(exist_ok=True)
    script = bin_directory / "npm"
    payload = writes if writes is not None else json.dumps(LOCK)
    if succeeds:
        script.write_text(
            "#!/bin/sh\n"
            f"cat > package-lock.json <<'LOCKEOF'\n{payload}\nLOCKEOF\n"
            "exit 0\n",
            encoding="utf-8",
        )
    else:
        script.write_text("#!/bin/sh\necho 'npm ERR! offline' >&2\nexit 1\n", encoding="utf-8")
    script.chmod(0o755)

    previous = os.environ.get("PATH", "")
    os.environ["PATH"] = f"{bin_directory}:{previous}"
    try:
        yield
    finally:
        os.environ["PATH"] = previous


@contextlib.contextmanager
def sides(root: Path) -> Iterator[tuple[Path, Path, Path]]:
    paths = []
    for name, body in (
        ("base", {"name": "pkg", "version": "base"}),
        ("ours", {"name": "pkg", "version": "ours"}),
        ("theirs", {"name": "pkg", "version": "theirs"}),
    ):
        path = root / name
        path.write_text(json.dumps(body), encoding="utf-8")
        paths.append(path)
    yield tuple(paths)  # type: ignore[misc]


def run(root: Path, base: Path, ours: Path, theirs: Path) -> int:
    os.environ["PRACTICE_TAKES_REPO_ROOT"] = str(root)
    try:
        return driver.main(
            [str(base), str(ours), str(theirs), "src/services/package-lock.json"]
        )
    finally:
        os.environ.pop("PRACTICE_TAKES_REPO_ROOT", None)


class RegenerationTests(unittest.TestCase):
    def test_a_successful_regeneration_is_written_over_ours(self) -> None:
        with workspace() as root, sides(root) as (base, ours, theirs):
            with stub_npm(root, succeeds=True):
                code = run(root, base, ours, theirs)
            result = json.loads(ours.read_text(encoding="utf-8"))

        self.assertEqual(code, 0)
        self.assertEqual(result["lockfileVersion"], 3)


class RefusalTests(unittest.TestCase):
    def test_a_failing_npm_leaves_git_to_conflict(self) -> None:
        with workspace() as root, sides(root) as (base, ours, theirs):
            with stub_npm(root, succeeds=False):
                code = run(root, base, ours, theirs)

        self.assertEqual(code, 1)

    def test_a_conflicted_manifest_is_refused(self) -> None:
        conflicted = '{\n<<<<<<< ours\n  "name": "a"\n=======\n  "name": "b"\n>>>>>>> theirs\n}\n'
        with workspace(conflicted) as root, sides(root) as (base, ours, theirs):
            with stub_npm(root, succeeds=True):
                code = run(root, base, ours, theirs)

        self.assertEqual(code, 1)

    def test_a_missing_manifest_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with sides(root) as (base, ours, theirs):
                code = run(root, base, ours, theirs)

        self.assertEqual(code, 1)

    def test_invalid_regenerated_json_is_refused(self) -> None:
        with workspace() as root, sides(root) as (base, ours, theirs):
            with stub_npm(root, succeeds=True, writes="not json at all"):
                code = run(root, base, ours, theirs)

        self.assertEqual(code, 1)

    def test_a_failed_regeneration_restores_the_working_lockfile(self) -> None:
        """The driver must not leave the checkout worse than it found it."""
        with workspace() as root, sides(root) as (base, ours, theirs):
            lockfile = root / "src" / "services" / "package-lock.json"
            lockfile.write_text('{"name": "original"}', encoding="utf-8")
            with stub_npm(root, succeeds=False):
                run(root, base, ours, theirs)
            restored = json.loads(lockfile.read_text(encoding="utf-8"))

        self.assertEqual(restored["name"], "original")


if __name__ == "__main__":
    unittest.main()
