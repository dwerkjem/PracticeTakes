#!/usr/bin/env python3
"""Run every ``test_*.py`` under ``scripts/``.

``python -m unittest discover`` cannot be used for this tree. The script
directories are not importable packages, so discovery reports ``NO TESTS RAN``
and exits successfully — a CI job wired to it would stay green while running
nothing. ``scripts/secrets`` would also shadow the standard library's
``secrets`` module if it were made importable by name.

This loader finds test files by path instead, so a test added in any
subdirectory runs without changing CI, and an empty result is an error rather
than a silent pass.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from types import ModuleType
import unittest

SCRIPTS_ROOT = Path(__file__).resolve().parent
MODULE_PREFIX = "practice_takes_script_tests"


def load_test_module(path: Path) -> ModuleType:
    """Import one test file under a unique name derived from its location."""
    relative = path.relative_to(SCRIPTS_ROOT).with_suffix("")
    name = f"{MODULE_PREFIX}.{'.'.join(relative.parts)}"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load test module: {path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def build_suite() -> unittest.TestSuite:
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    paths = sorted(
        path
        for path in SCRIPTS_ROOT.rglob("test_*.py")
        if "__pycache__" not in path.parts
    )

    if not paths:
        raise SystemExit(f"error: no test files were found under {SCRIPTS_ROOT}")

    for path in paths:
        suite.addTests(loader.loadTestsFromModule(load_test_module(path)))
    return suite


def main() -> int:
    result = unittest.TextTestRunner(verbosity=2).run(build_suite())
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
