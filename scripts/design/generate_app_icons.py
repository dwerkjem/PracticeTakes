#!/usr/bin/env python3
"""Render the Practice Takes application icon from its design-system source.

The icon lives in the "Design System" Claude Design project as ``ICO.html``
(dark), ``ICO-light.html`` (light), and ``thumbnail.html`` (identical artwork to
``ICO.html``). Those sources are CSS layouts on a 1024x1024 canvas: a flat
background, two faint staff systems carrying stemmed notes, and a "PT" monogram
set in IBM Plex Mono SemiBold under a linear gradient.

Rendering them would need a browser, and drawing them would need a font, so this
module restates their geometry as data instead. The two monogram outlines are
transcribed from IBM Plex Mono SemiBold (SIL OFL 1.1) in font units, which keeps
both the raster and the vector output self-contained.

Outputs, regenerated in place and committed:

    packaging/icons/practice-takes.svg
    packaging/icons/practice-takes-<size>.png
    packaging/icons/practice-takes.ico
    packaging/icons/practice-takes-light.svg
    packaging/icons/practice-takes-light-<size>.png

Run from the repository root:

    uv run --no-project --with pillow scripts/design/generate_app_icons.py

Pillow is imported lazily, so every geometry helper below stays importable — and
unit-testable — without it.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
from pathlib import Path
from typing import Sequence

REPO_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIRECTORY = REPO_ROOT / "packaging" / "icons"

CANVAS = 1024
"""Both source layouts render into a 1024x1024 body with ``overflow: hidden``."""

SUPERSAMPLE = 4
"""Masks are filled at this multiple of the canvas, then resampled down."""

ICON_SIZES = (1024, 512, 256, 128, 64, 48, 32)
LIGHT_ICON_SIZES = (1024, 512)
ICO_SIZES = (256, 128, 64, 48, 32)
"""Windows .ico entries. The format tops out at 256x256."""

# --- Staff --------------------------------------------------------------------
# `.sys` is 96px tall and painted with
# `repeating-linear-gradient(to bottom, accent 0 2px, transparent 2px 24px)`,
# so it carries a 2px rule every 24px until the element clips the run.
SYSTEM_HEIGHT = 96
SYSTEM_GAP = 88
SYSTEM_COUNT = 2
STAFF_LINE_THICKNESS = 2
STAFF_LINE_SPACING = 24

# `.note` is a 34x24 ellipse rotated -20deg about its own centre.
NOTE_WIDTH = 34
NOTE_HEIGHT = 24
NOTE_ROTATION_DEGREES = -20

# `.note::after` is the stem: it inherits the notehead's -20deg rotation and then
# applies +20deg of its own, so it ends up axis-aligned but displaced.
STEM_WIDTH = 3
STEM_HEIGHT = 64
STEM_RIGHT_INSET = 1
STEM_TOP = -62
STEM_ROTATION_DEGREES = 20

# Inline `left`/`top` offsets, per system, relative to that system's box. The
# last note of each row sits beyond the canvas and is clipped, exactly as the
# browser clips it.
NOTE_OFFSETS: tuple[tuple[tuple[int, int], ...], ...] = (
    ((120, 60), (300, 12), (470, 36), (700, 0), (900, 48), (1090, 24)),
    ((180, 24), (390, 60), (600, 36), (820, 72), (1010, 12)),
)

NOTE_SEGMENTS = 96
"""Sides used to approximate a rotated notehead ellipse."""

# --- Monogram -----------------------------------------------------------------
FONT_UNITS_PER_EM = 1000
MARK_TEXT = "PT"
MARK_FONT_SIZE = 400
MARK_LETTER_SPACING_EM = -0.06
MARK_LINE_HEIGHT = 0.8
MARK_SCALE = MARK_FONT_SIZE / FONT_UNITS_PER_EM

GRADIENT_ANGLE_DEGREES = 160
GLOW_BLUR_RADIUS = 60
"""CSS ``text-shadow`` blur radius; a Gaussian sigma is half of it."""

# IBM Plex Mono SemiBold outlines in font units, y up from the baseline. Each
# contour is a list of segments: ("line", end) or ("quad", control, end), with
# TrueType's implied on-curve midpoints already made explicit.
GLYPH_ADVANCE = 600
GLYPH_OUTLINES: dict[str, tuple[tuple[tuple[float, float], tuple, ...], ...]] = {
    "P": (
        (
            (80.0, 0.0),
            ("line", (80.0, 698.0)),
            ("line", (345.0, 698.0)),
            ("quad", (447.0, 698.0), (501.0, 640.0)),
            ("quad", (555.0, 582.0), (555.0, 482.0)),
            ("quad", (555.0, 382.0), (501.0, 324.0)),
            ("quad", (447.0, 266.0), (345.0, 266.0)),
            ("line", (211.0, 266.0)),
            ("line", (211.0, 0.0)),
        ),
        (
            (211.0, 373.0),
            ("line", (318.0, 373.0)),
            ("quad", (371.0, 373.0), (394.0, 394.5)),
            ("quad", (417.0, 416.0), (417.0, 463.0)),
            ("line", (417.0, 501.0)),
            ("quad", (417.0, 548.0), (394.0, 569.5)),
            ("quad", (371.0, 591.0), (318.0, 591.0)),
            ("line", (211.0, 591.0)),
        ),
    ),
    "T": (
        (
            (365.0, 590.0),
            ("line", (365.0, 0.0)),
            ("line", (235.0, 0.0)),
            ("line", (235.0, 590.0)),
            ("line", (25.0, 590.0)),
            ("line", (25.0, 698.0)),
            ("line", (575.0, 698.0)),
            ("line", (575.0, 590.0)),
        ),
    ),
}

QUADRATIC_STEPS = 24
"""Line segments per quadratic curve when flattening for the rasteriser."""


@dataclass(frozen=True)
class Palette:
    """One rendered variant of the icon."""

    name: str
    background: str
    staff: str
    staff_opacity: float
    gradient: tuple[tuple[float, str], ...]
    glow: tuple[str, float] | None


DARK = Palette(
    name="practice-takes",
    background="#12141B",
    staff="#64AAFF",
    staff_opacity=0.3,
    gradient=((0.0, "#EEF1F7"), (0.55, "#9EC9FF"), (1.0, "#64AAFF")),
    glow=("#64AAFF", 0.18),
)

LIGHT = Palette(
    name="practice-takes-light",
    background="#FAFAFB",
    staff="#3770C4",
    staff_opacity=0.55,
    gradient=((0.0, "#1C1F25"), (0.55, "#2E5FA8"), (1.0, "#3770C4")),
    glow=None,
)


Point = tuple[float, float]
Rectangle = tuple[float, float, float, float]
Polygon = list[Point]


# --- Staff geometry -----------------------------------------------------------


def staff_line_offsets() -> tuple[int, ...]:
    """Offsets of each staff rule from the top of its system."""
    offsets = []
    offset = 0
    while offset + STAFF_LINE_THICKNESS <= SYSTEM_HEIGHT:
        offsets.append(offset)
        offset += STAFF_LINE_SPACING
    return tuple(offsets)


def staff_block_height() -> int:
    """Combined height of every system plus the gaps between them."""
    return SYSTEM_COUNT * SYSTEM_HEIGHT + (SYSTEM_COUNT - 1) * SYSTEM_GAP


def system_tops() -> tuple[float, ...]:
    """Canvas y of each system's top edge; the block is centred vertically."""
    first = (CANVAS - staff_block_height()) / 2
    return tuple(first + index * (SYSTEM_HEIGHT + SYSTEM_GAP) for index in range(SYSTEM_COUNT))


def staff_line_rectangles() -> list[Rectangle]:
    """Every staff rule, spanning the full canvas width."""
    rectangles = []
    for top in system_tops():
        for offset in staff_line_offsets():
            y = top + offset
            rectangles.append((0.0, y, float(CANVAS), y + STAFF_LINE_THICKNESS))
    return rectangles


def _rotate(point: Point, origin: Point, degrees: float) -> Point:
    radians = math.radians(degrees)
    cosine, sine = math.cos(radians), math.sin(radians)
    dx, dy = point[0] - origin[0], point[1] - origin[1]
    return (
        origin[0] + dx * cosine - dy * sine,
        origin[1] + dx * sine + dy * cosine,
    )


def note_centres() -> list[Point]:
    """Canvas centre of every notehead, in source order."""
    centres = []
    for top, offsets in zip(system_tops(), NOTE_OFFSETS):
        for left, note_top in offsets:
            centres.append((left + NOTE_WIDTH / 2, top + note_top + NOTE_HEIGHT / 2))
    return centres


def note_polygon(centre: Point, segments: int = NOTE_SEGMENTS) -> Polygon:
    """The rotated notehead ellipse, approximated as a polygon."""
    radius_x, radius_y = NOTE_WIDTH / 2, NOTE_HEIGHT / 2
    points = []
    for index in range(segments):
        angle = 2 * math.pi * index / segments
        point = (centre[0] + radius_x * math.cos(angle), centre[1] + radius_y * math.sin(angle))
        points.append(_rotate(point, centre, NOTE_ROTATION_DEGREES))
    return points


def stem_rectangle(centre: Point) -> Rectangle:
    """The stem belonging to the notehead at ``centre``.

    The stem's two nested rotations cancel, so the result is axis-aligned; only
    its centre has to be carried through the notehead's rotation.
    """
    note_centre = (NOTE_WIDTH / 2, NOTE_HEIGHT / 2)
    stem_centre = (
        NOTE_WIDTH - STEM_RIGHT_INSET - STEM_WIDTH / 2,
        STEM_TOP + STEM_HEIGHT / 2,
    )
    rotated = _rotate(stem_centre, note_centre, NOTE_ROTATION_DEGREES)
    x = centre[0] + rotated[0] - note_centre[0]
    y = centre[1] + rotated[1] - note_centre[1]
    return (x - STEM_WIDTH / 2, y - STEM_HEIGHT / 2, x + STEM_WIDTH / 2, y + STEM_HEIGHT / 2)


def staff_polygons() -> list[Polygon]:
    """Every shape in the staff layer, which is painted as one 30%-opacity group."""
    polygons = [_rectangle_polygon(rectangle) for rectangle in staff_line_rectangles()]
    for centre in note_centres():
        polygons.append(note_polygon(centre))
        polygons.append(_rectangle_polygon(stem_rectangle(centre)))
    return polygons


def _rectangle_polygon(rectangle: Rectangle) -> Polygon:
    left, top, right, bottom = rectangle
    return [(left, top), (right, top), (right, bottom), (left, bottom)]


# --- Monogram geometry --------------------------------------------------------


def _glyph_origins() -> list[float]:
    """Pen x for each character, in font units, including letter spacing."""
    spacing = MARK_LETTER_SPACING_EM * FONT_UNITS_PER_EM
    return [index * (GLYPH_ADVANCE + spacing) for index in range(len(MARK_TEXT))]


def mark_box() -> tuple[float, float]:
    """Size of the ``.mark`` element box, which the gradient is measured across.

    CSS adds letter spacing after the final character too, so the box is wider
    than the ink it holds.
    """
    spacing = MARK_LETTER_SPACING_EM * MARK_FONT_SIZE
    width = len(MARK_TEXT) * (GLYPH_ADVANCE * MARK_SCALE + spacing)
    return (width, MARK_LINE_HEIGHT * MARK_FONT_SIZE)


def _untranslated_contours() -> list[list]:
    """Monogram contours in canvas units, with the pen at the origin."""
    contours: list[list] = []
    for origin, character in zip(_glyph_origins(), MARK_TEXT):
        for outline in GLYPH_OUTLINES[character]:
            start = outline[0]
            contour: list = [("move", _place(start, origin))]
            for segment in outline[1:]:
                if segment[0] == "line":
                    contour.append(("line", _place(segment[1], origin)))
                else:
                    contour.append(
                        ("quad", _place(segment[1], origin), _place(segment[2], origin))
                    )
            contours.append(contour)
    return contours


def _place(point: Point, origin: float) -> Point:
    """Map a font-unit point onto the canvas, with y running downwards."""
    return ((origin + point[0]) * MARK_SCALE, -point[1] * MARK_SCALE)


def _bounds(points: Sequence[Point]) -> Rectangle:
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    return (min(xs), min(ys), max(xs), max(ys))


def mark_translation() -> Point:
    """Offset that centres the monogram's ink on the canvas.

    The source centres the *element*, which the trailing letter space pushes
    about 23px right of the ink's own centre. At icon sizes that reads as a
    mistake, so the ink box is centred instead; nothing else about the mark
    changes.
    """
    points: list[Point] = []
    for contour in _untranslated_contours():
        for segment in contour:
            points.extend(segment[1:])
    left, top, right, bottom = _bounds(points)
    return (CANVAS / 2 - (left + right) / 2, CANVAS / 2 - (top + bottom) / 2)


def mark_contours() -> list[list]:
    """Monogram contours placed on the canvas."""
    offset_x, offset_y = mark_translation()

    def shift(point: Point) -> Point:
        return (point[0] + offset_x, point[1] + offset_y)

    return [
        [(segment[0], *(shift(point) for point in segment[1:])) for segment in contour]
        for contour in _untranslated_contours()
    ]


def flatten_contour(contour: Sequence, steps: int = QUADRATIC_STEPS) -> Polygon:
    """Approximate one contour as a closed polygon."""
    points: Polygon = [contour[0][1]]
    for segment in contour[1:]:
        if segment[0] == "line":
            points.append(segment[1])
            continue
        start = points[-1]
        control, end = segment[1], segment[2]
        for step in range(1, steps + 1):
            t = step / steps
            inverse = 1 - t
            points.append(
                (
                    inverse * inverse * start[0] + 2 * inverse * t * control[0] + t * t * end[0],
                    inverse * inverse * start[1] + 2 * inverse * t * control[1] + t * t * end[1],
                )
            )
    return points


# --- Gradient -----------------------------------------------------------------


def _direction(degrees: float) -> Point:
    """CSS gradient direction: 0deg points up, and angles increase clockwise."""
    radians = math.radians(degrees)
    return (math.sin(radians), -math.cos(radians))


def gradient_line() -> tuple[Point, Point]:
    """Start and end of the gradient line across the ``.mark`` box."""
    width, height = mark_box()
    direction = _direction(GRADIENT_ANGLE_DEGREES)
    length = abs(width * direction[0]) + abs(height * direction[1])
    centre = (CANVAS / 2, CANVAS / 2)
    half = length / 2
    return (
        (centre[0] - direction[0] * half, centre[1] - direction[1] * half),
        (centre[0] + direction[0] * half, centre[1] + direction[1] * half),
    )


def gradient_projector():
    """Build a function mapping a canvas point to its 0..1 gradient offset."""
    start, end = gradient_line()
    span_x, span_y = end[0] - start[0], end[1] - start[1]
    length_squared = span_x * span_x + span_y * span_y

    def project(point: Point) -> float:
        projection = (
            (point[0] - start[0]) * span_x + (point[1] - start[1]) * span_y
        ) / length_squared
        return min(1.0, max(0.0, projection))

    return project


def gradient_offset(point: Point) -> float:
    """Position of ``point`` along the gradient line, clamped to 0..1."""
    return gradient_projector()(point)


def parse_colour(value: str) -> tuple[int, int, int]:
    """Parse a ``#rrggbb`` string."""
    text = value.lstrip("#")
    if len(text) != 6:
        raise ValueError(f"expected a #rrggbb colour, got {value!r}")
    return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16))


def gradient_colour(stops: Sequence[tuple[float, str]], offset: float) -> tuple[int, int, int]:
    """Interpolate ``stops`` in sRGB, matching how the source gradient renders."""
    offset = min(1.0, max(0.0, offset))
    if offset <= stops[0][0]:
        return parse_colour(stops[0][1])
    for (start_offset, start_colour), (end_offset, end_colour) in zip(stops, stops[1:]):
        if offset <= end_offset:
            span = end_offset - start_offset
            t = 0.0 if span == 0 else (offset - start_offset) / span
            first, second = parse_colour(start_colour), parse_colour(end_colour)
            return tuple(round(a + (b - a) * t) for a, b in zip(first, second))
    return parse_colour(stops[-1][1])


# --- SVG ----------------------------------------------------------------------


def _number(value: float) -> str:
    return f"{value:.2f}".rstrip("0").rstrip(".")


def mark_path_data() -> str:
    """The monogram as SVG path data, with curves kept as curves."""
    commands = []
    for contour in mark_contours():
        for index, segment in enumerate(contour):
            if index == 0:
                commands.append(f"M{_number(segment[1][0])} {_number(segment[1][1])}")
            elif segment[0] == "line":
                commands.append(f"L{_number(segment[1][0])} {_number(segment[1][1])}")
            else:
                control, end = segment[1], segment[2]
                commands.append(
                    f"Q{_number(control[0])} {_number(control[1])}"
                    f" {_number(end[0])} {_number(end[1])}"
                )
        commands.append("Z")
    return "".join(commands)


def build_svg(palette: Palette) -> str:
    """Render one variant as a self-contained SVG."""
    start, end = gradient_line()
    stops = "".join(
        f'<stop offset="{_number(offset * 100)}%" stop-color="{colour}"/>'
        for offset, colour in palette.gradient
    )
    gradient_id = f"{palette.name}-mark"

    defs = [
        f'<linearGradient id="{gradient_id}" gradientUnits="userSpaceOnUse"'
        f' x1="{_number(start[0])}" y1="{_number(start[1])}"'
        f' x2="{_number(end[0])}" y2="{_number(end[1])}">{stops}</linearGradient>'
    ]

    path_data = mark_path_data()
    glow = ""
    if palette.glow is not None:
        colour, opacity = palette.glow
        defs.append(
            f'<filter id="{palette.name}-glow" filterUnits="userSpaceOnUse"'
            f' x="0" y="0" width="{CANVAS}" height="{CANVAS}">'
            f'<feGaussianBlur stdDeviation="{_number(GLOW_BLUR_RADIUS / 2)}"/></filter>'
        )
        glow = (
            f'<path d="{path_data}" fill="{colour}" fill-opacity="{_number(opacity)}"'
            f' fill-rule="evenodd" filter="url(#{palette.name}-glow)"/>'
        )

    shapes = []
    for left, top, right, bottom in staff_line_rectangles():
        shapes.append(
            f'<rect x="{_number(left)}" y="{_number(top)}"'
            f' width="{_number(right - left)}" height="{_number(bottom - top)}"/>'
        )
    for centre in note_centres():
        shapes.append(
            f'<ellipse cx="{_number(centre[0])}" cy="{_number(centre[1])}"'
            f' rx="{_number(NOTE_WIDTH / 2)}" ry="{_number(NOTE_HEIGHT / 2)}"'
            f' transform="rotate({NOTE_ROTATION_DEGREES}'
            f' {_number(centre[0])} {_number(centre[1])})"/>'
        )
        left, top, right, bottom = stem_rectangle(centre)
        shapes.append(
            f'<rect x="{_number(left)}" y="{_number(top)}"'
            f' width="{_number(right - left)}" height="{_number(bottom - top)}"/>'
        )

    return (
        '<svg xmlns="http://www.w3.org/2000/svg"'
        f' width="{CANVAS}" height="{CANVAS}" viewBox="0 0 {CANVAS} {CANVAS}">'
        f"<defs>{''.join(defs)}</defs>"
        f'<rect width="{CANVAS}" height="{CANVAS}" fill="{palette.background}"/>'
        f'<g fill="{palette.staff}" opacity="{_number(palette.staff_opacity)}">'
        f"{''.join(shapes)}</g>"
        f"{glow}"
        f'<path d="{path_data}" fill="url(#{gradient_id})" fill-rule="evenodd"/>'
        "</svg>\n"
    )


# --- Rasterisation ------------------------------------------------------------


def _pillow():
    try:
        from PIL import Image, ImageChops, ImageDraw, ImageFilter
    except ModuleNotFoundError as error:  # pragma: no cover - depends on the environment
        raise SystemExit(
            "Pillow is required to render the icons. Run:\n"
            "  uv run --no-project --with pillow scripts/design/generate_app_icons.py"
        ) from error
    return Image, ImageChops, ImageDraw, ImageFilter


def _filled_mask(polygons: Sequence[Polygon], even_odd: bool = False):
    """Fill ``polygons`` supersampled, then resample to canvas resolution.

    With ``even_odd`` each polygon is a contour of one shape and overlaps punch
    holes; otherwise the polygons are unioned.
    """
    Image, ImageChops, ImageDraw, _ = _pillow()
    size = CANVAS * SUPERSAMPLE
    mask = Image.new("L", (size, size), 0)

    for polygon in polygons:
        scaled = [(x * SUPERSAMPLE, y * SUPERSAMPLE) for x, y in polygon]
        if even_odd:
            contour = Image.new("L", (size, size), 0)
            ImageDraw.Draw(contour).polygon(scaled, fill=255)
            mask = ImageChops.difference(mask, contour)
        else:
            ImageDraw.Draw(mask).polygon(scaled, fill=255)

    return mask.resize((CANVAS, CANVAS), Image.LANCZOS)


def _scaled_mask(mask, opacity: float):
    return mask.point(lambda value: round(value * opacity))


GRADIENT_LOOKUP_STEPS = 512
"""Sampled gradient colours. The gradient line is shorter than this in pixels."""


def _gradient_image(palette: Palette):
    Image, _, _, _ = _pillow()
    project = gradient_projector()
    lookup = [
        gradient_colour(palette.gradient, step / (GRADIENT_LOOKUP_STEPS - 1))
        for step in range(GRADIENT_LOOKUP_STEPS)
    ]
    last = GRADIENT_LOOKUP_STEPS - 1
    pixels = [
        lookup[round(project((x + 0.5, y + 0.5)) * last)]
        for y in range(CANVAS)
        for x in range(CANVAS)
    ]
    image = Image.new("RGB", (CANVAS, CANVAS))
    image.putdata(pixels)
    return image


def render_master(palette: Palette):
    """Render one variant at full canvas resolution."""
    Image, _, _, ImageFilter = _pillow()

    canvas = Image.new("RGB", (CANVAS, CANVAS), palette.background)

    staff = _filled_mask(staff_polygons())
    canvas.paste(
        Image.new("RGB", (CANVAS, CANVAS), palette.staff),
        (0, 0),
        _scaled_mask(staff, palette.staff_opacity),
    )

    mark = _filled_mask([flatten_contour(contour) for contour in mark_contours()], even_odd=True)

    if palette.glow is not None:
        colour, opacity = palette.glow
        blurred = mark.filter(ImageFilter.GaussianBlur(GLOW_BLUR_RADIUS / 2))
        canvas.paste(
            Image.new("RGB", (CANVAS, CANVAS), colour), (0, 0), _scaled_mask(blurred, opacity)
        )

    canvas.paste(_gradient_image(palette), (0, 0), mark)
    return canvas.convert("RGBA")


def write_variant(palette: Palette, sizes: Sequence[int], directory: Path) -> list[Path]:
    """Write the SVG and every PNG size for one variant."""
    Image, _, _, _ = _pillow()

    written = []
    svg_path = directory / f"{palette.name}.svg"
    svg_path.write_text(build_svg(palette), encoding="utf-8")
    written.append(svg_path)

    master = render_master(palette)
    for size in sizes:
        image = master if size == CANVAS else master.resize((size, size), Image.LANCZOS)
        png_path = directory / f"{palette.name}-{size}.png"
        image.save(png_path, "PNG", optimize=True)
        written.append(png_path)

    return written


def write_windows_icon(palette: Palette, directory: Path) -> Path:
    """Write the multi-resolution .ico the Windows installer uses."""
    Image, _, _, _ = _pillow()
    master = render_master(palette)
    path = directory / f"{palette.name}.ico"
    master.resize((max(ICO_SIZES), max(ICO_SIZES)), Image.LANCZOS).save(
        path, "ICO", sizes=[(size, size) for size in ICO_SIZES]
    )
    return path


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--output-directory",
        type=Path,
        default=OUTPUT_DIRECTORY,
        help="where the icon files are written (default: packaging/icons)",
    )
    arguments = parser.parse_args(argv)

    directory = arguments.output_directory
    directory.mkdir(parents=True, exist_ok=True)

    written = write_variant(DARK, ICON_SIZES, directory)
    written.append(write_windows_icon(DARK, directory))
    written.extend(write_variant(LIGHT, LIGHT_ICON_SIZES, directory))

    for path in written:
        print(path.relative_to(REPO_ROOT) if REPO_ROOT in path.parents else path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
