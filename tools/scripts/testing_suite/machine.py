#!/usr/bin/env python3
"""What machine a run happened on.

A performance number without the machine that produced it is not evidence, so
every run carries one of these. The interesting decision is what counts as "the
same machine": identity is hashed from hardware facts that do not change under
routine maintenance, and the volatile detail — kernel, distribution version,
driver versions — is recorded as an attribute instead.

That split is load-bearing. Fold the kernel version into identity and every
system upgrade silently starts a new machine, which throws away exactly the
comparison history the store exists to keep.

Standard library only. Every probe degrades to "unknown" rather than failing:
missing provenance detail is worth recording a run without, and a suite that
refuses to start because `glxinfo` is absent helps nobody.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import platform
import re
import subprocess

UNKNOWN = "unknown"

# The fields identity is hashed from. Adding one here means every existing
# machine gets a new identity, so it is a deliberate act rather than a tweak.
IDENTITY_FIELDS = (
    "processor",
    "cores",
    "memory_bytes",
    "graphics",
    "operating_system",
    "display",
)


def _command(arguments: list[str], timeout: float = 5.0) -> str:
    try:
        completed = subprocess.run(
            arguments, capture_output=True, text=True, timeout=timeout, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return ""

    return completed.stdout if completed.returncode == 0 else ""


def processor() -> str:
    try:
        text = Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return platform.processor() or UNKNOWN

    for line in text.splitlines():
        if line.lower().startswith("model name"):
            return line.split(":", 1)[1].strip()

    return platform.processor() or UNKNOWN


def cores() -> int:
    return os.cpu_count() or 0


def memory_bytes() -> int:
    """Rounded to whole gibibytes.

    The kernel reports slightly different MemTotal values across boots — a few
    hundred kilobytes reserved differently is enough — and an unrounded value
    would make identity change for no reason anyone cares about.
    """
    try:
        text = Path("/proc/meminfo").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0

    match = re.search(r"^MemTotal:\s+(\d+)\s+kB", text, re.MULTILINE)

    if not match:
        return 0

    gibibytes = round(int(match.group(1)) * 1024 / (1024**3))

    return gibibytes * 1024**3


def graphics() -> str:
    output = _command(["glxinfo", "-B"])

    for line in output.splitlines():
        if "OpenGL renderer string" in line:
            return line.split(":", 1)[1].strip()

    output = _command(["lspci"])

    for line in output.splitlines():
        if "VGA compatible controller" in line:
            return line.split(":", 2)[-1].strip()

    return UNKNOWN


def operating_system() -> str:
    """The distribution name without its version.

    Version belongs in attributes: upgrading from one release to the next is
    maintenance, not new hardware.
    """
    try:
        text = Path("/etc/os-release").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return platform.system() or UNKNOWN

    match = re.search(r'^NAME="?([^"\n]+)"?', text, re.MULTILINE)
    name = match.group(1).strip() if match else platform.system()

    return f"{platform.system()} {name}".strip() or UNKNOWN


def display() -> str:
    output = _command(["xdpyinfo"])
    match = re.search(r"dimensions:\s+(\d+x\d+)", output)

    if match:
        return match.group(1)

    output = _command(["xrandr", "--current"])
    match = re.search(r"current (\d+) x (\d+)", output)

    if match:
        return f"{match.group(1)}x{match.group(2)}"

    return UNKNOWN


def attributes() -> dict[str, str]:
    """Volatile detail: recorded, but deliberately outside identity."""
    values = {
        "kernel": platform.release() or UNKNOWN,
        "python": platform.python_version(),
    }

    try:
        text = Path("/etc/os-release").read_text(encoding="utf-8", errors="replace")
        match = re.search(r'^VERSION_ID="?([^"\n]+)"?', text, re.MULTILINE)

        if match:
            values["distribution_version"] = match.group(1).strip()
    except OSError:
        pass

    output = _command(["glxinfo", "-B"])

    for line in output.splitlines():
        if "OpenGL version string" in line:
            values["graphics_driver"] = line.split(":", 1)[1].strip()

            break

    return values


def identity_of(facts: dict) -> str:
    """A stable hash over the identity fields, and nothing else."""
    material = "\n".join(f"{field}={facts.get(field, '')}" for field in IDENTITY_FIELDS)

    return hashlib.sha256(material.encode("utf-8")).hexdigest()[:16]


def provenance() -> dict:
    """Everything a run records about the machine it ran on."""
    facts = {
        "processor": processor(),
        "cores": cores(),
        "memory_bytes": memory_bytes(),
        "graphics": graphics(),
        "operating_system": operating_system(),
        "display": display(),
    }
    facts["identity"] = identity_of(facts)
    facts["attributes"] = attributes()

    return facts


def describe(facts: dict) -> str:
    memory = int(facts.get("memory_bytes", 0)) // 1024**3

    return (
        f"{facts.get('processor', UNKNOWN)} · {facts.get('cores', 0)} cores · "
        f"{memory} GiB · {facts.get('graphics', UNKNOWN)} · "
        f"{facts.get('operating_system', UNKNOWN)} · {facts.get('display', UNKNOWN)}"
    )
