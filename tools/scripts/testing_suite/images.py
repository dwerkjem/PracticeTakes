#!/usr/bin/env python3
"""Turning a captured window into something a browser can show.

`xwindow_capture` writes a P6 PPM, which is the right thing for it to write —
no dependencies, no encoder, no ambiguity about colour. It is also several
megabytes per window and unrenderable in a browser, so every capture is
converted to PNG and thumbnailed here, and the PPM is discarded.

Pillow does the work. It is the one real dependency the suite takes, scoped to
the `testing-suite` dependency group so the standard-library-only commit gate
and CI scripts never see it. The alternative — a hand-rolled PNG encoder on
`zlib` — is fine until it has to downscale a million pixels per thumbnail in
pure Python, which is where it stops being the cheaper option.

Imported lazily so that the store, the plan, and the exporter stay importable
(and testable) on a machine that has never installed it.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

# Wide enough to judge layout in the grid, small enough that forty of them
# decode instantly. The full-size PNG is what zoom opens.
THUMBNAIL_WIDTH = 480


class ImageError(RuntimeError):
    """A capture could not be converted."""


def _pillow():
    try:
        from PIL import Image  # noqa: PLC0415 — deliberately lazy; see the module docstring
    except ImportError as error:  # pragma: no cover - depends on the environment
        raise ImageError(
            "Pillow is not installed. Install the testing-suite dependency group "
            "(`uv sync`) before running a capture pass."
        ) from error

    return Image


def digest_of(path: Path) -> str:
    hasher = hashlib.sha256()

    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            hasher.update(block)

    return hasher.hexdigest()


def convert(source: Path, png_path: Path, thumbnail_path: Path) -> tuple[int, int, str]:
    """PPM in, PNG plus thumbnail out. Returns width, height, and the PNG digest."""
    Image = _pillow()

    png_path.parent.mkdir(parents=True, exist_ok=True)
    thumbnail_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        with Image.open(source) as image:
            image = image.convert("RGB")
            width, height = image.size
            image.save(png_path, format="PNG", optimize=True)

            scale = min(1.0, THUMBNAIL_WIDTH / float(width or 1))
            thumbnail = image.resize(
                (max(1, int(width * scale)), max(1, int(height * scale))),
                Image.LANCZOS,
            )
            thumbnail.save(thumbnail_path, format="PNG", optimize=True)
    except OSError as error:
        raise ImageError(f"could not convert {source}: {error}") from error

    return width, height, digest_of(png_path)


def read_ppm_size(path: Path) -> tuple[int, int]:
    """The dimensions in a P6 header, without decoding the pixels.

    Used to check a capture is the size that was asked for before spending
    anything on conversion.
    """
    try:
        with path.open("rb") as handle:
            fields: list[bytes] = []

            while len(fields) < 4:
                token = b""
                character = handle.read(1)

                if not character:
                    raise ImageError(f"{path} ended inside its header")

                if character == b"#":
                    while character not in (b"\n", b""):
                        character = handle.read(1)

                    continue

                if character.isspace():
                    continue

                while character and not character.isspace():
                    token += character
                    character = handle.read(1)

                fields.append(token)

        if fields[0] != b"P6":
            raise ImageError(f"{path} is not a P6 PPM")

        return int(fields[1]), int(fields[2])
    except (OSError, ValueError, IndexError) as error:
        raise ImageError(f"could not read {path}: {error}") from error
