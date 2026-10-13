#!/usr/bin/env python3
"""Generate the engraving glyph metrics table from Bravura's SMuFL metadata.

The engraving layer is JUCE-free so that every layout decision can be asserted
without a display (``openspec/changes/engraved-notation-renderer/design.md``
decision 1). That rules out asking the font for a glyph's advance width at
layout time, because doing so would drag a typeface -- and therefore JUCE --
into the layer. So the widths are a static table, and this is what writes it.

The input is ``Bravura.json`` from the Bravura distribution, which is 1.25 MB of
metadata for 3467 glyphs. Committing all of it to carry the thirty this renderer
draws would be absurd, so the JSON is *not* in the repository: fetch it, run
this, commit the generated header. The URL and the version are recorded in the
header's banner so the table can always be traced back.

    curl -o /tmp/Bravura.json \\
        https://raw.githubusercontent.com/steinbergmedia/bravura/master/redist/Bravura.json
    curl -o /tmp/glyphnames.json \\
        https://raw.githubusercontent.com/w3c/smufl/gh-pages/metadata/glyphnames.json
    python3 tools/scripts/build/generate_glyph_metrics.py /tmp/Bravura.json /tmp/glyphnames.json

Pass --check to verify the committed header matches what the metadata would
produce, without writing anything.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys

SOURCE_URL = (
    "https://raw.githubusercontent.com/steinbergmedia/bravura/master/redist/Bravura.json"
)

# Codepoints are not in the font's own metadata -- they are SMuFL's, shared by
# every conforming font, so they come from the specification rather than from
# Bravura.
GLYPHNAMES_URL = (
    "https://raw.githubusercontent.com/w3c/smufl/gh-pages/metadata/glyphnames.json"
)

OUTPUT = pathlib.Path("src/platform/score/engraving/GlyphMetrics.h")

# The glyphs this pass draws, as SMuFL canonical names paired with the
# identifier the C++ enum uses. Anything the renderer cannot draw yet is absent
# on purpose: an unused entry in this table is an invitation to draw something
# the layout has not been taught to place.
GLYPHS: list[tuple[str, str]] = [
    # Clefs. The octave-transposed variants are not here; Clef::octaveChange is
    # read for sounding pitch and drawn as the plain sign for now.
    ("gClef", "gClef"),
    ("fClef", "fClef"),
    ("cClef", "cClef"),
    # Noteheads.
    ("noteheadWhole", "noteheadWhole"),
    ("noteheadHalf", "noteheadHalf"),
    ("noteheadBlack", "noteheadBlack"),
    # Accidentals. Double sharp and double flat are included because the model
    # keeps spellings precisely so they stay distinguishable (Pitch.h).
    ("accidentalDoubleFlat", "accidentalDoubleFlat"),
    ("accidentalFlat", "accidentalFlat"),
    ("accidentalNatural", "accidentalNatural"),
    ("accidentalSharp", "accidentalSharp"),
    ("accidentalDoubleSharp", "accidentalDoubleSharp"),
    # Rests.
    ("restWhole", "restWhole"),
    ("restHalf", "restHalf"),
    ("restQuarter", "restQuarter"),
    ("rest8th", "rest8th"),
    ("rest16th", "rest16th"),
    # Flags.
    ("flag8thUp", "flag8thUp"),
    ("flag8thDown", "flag8thDown"),
    ("flag16thUp", "flag16thUp"),
    ("flag16thDown", "flag16thDown"),
    # Time signature digits.
    ("timeSig0", "timeSig0"),
    ("timeSig1", "timeSig1"),
    ("timeSig2", "timeSig2"),
    ("timeSig3", "timeSig3"),
    ("timeSig4", "timeSig4"),
    ("timeSig5", "timeSig5"),
    ("timeSig6", "timeSig6"),
    ("timeSig7", "timeSig7"),
    ("timeSig8", "timeSig8"),
    ("timeSig9", "timeSig9"),
    # The brace that makes a grand staff a grand staff, and the bracket that
    # groups separate parts.
    ("brace", "brace"),
    ("bracket", "bracket"),
    # Augmentation dot.
    ("augmentationDot", "augmentationDot"),
]

# Engraving defaults the layout uses. SMuFL states all of these in staff
# spaces, which is the unit the layout works in, so they pass through unscaled.
DEFAULTS: list[tuple[str, str]] = [
    ("staffLineThickness", "staffLineThickness"),
    ("stemThickness", "stemThickness"),
    ("legerLineThickness", "legerLineThickness"),
    ("legerLineExtension", "legerLineExtension"),
    ("thinBarlineThickness", "thinBarlineThickness"),
    ("thickBarlineThickness", "thickBarlineThickness"),
    ("barlineSeparation", "barlineSeparation"),
    ("bracketThickness", "bracketThickness"),
    ("beamThickness", "beamThickness"),
    ("beamSpacing", "beamSpacing"),
]


def clang_format(text: str) -> str:
    """Run the generated text through clang-format.

    Without this the generator and the pre-commit hook fight: clang-format
    reformats the committed header, and the next ``--check`` then reports it out
    of date even though nothing about the font changed. Formatting here makes
    the generator's output already final, so the two agree.

    The tool is looked up the way the other quality scripts do it, through
    CLANG_FORMAT, so a pinned binary can be pointed at.
    """
    tool = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")

    if tool is None:
        print(
            "clang-format not found; emitting unformatted output. The pre-commit "
            "hook will reformat it and --check will then disagree.",
            file=sys.stderr,
        )

        return text

    completed = subprocess.run(
        [tool, f"--assume-filename={OUTPUT}"],
        input=text,
        capture_output=True,
        text=True,
        check=True,
    )

    return completed.stdout


def render(metadata: dict, glyphnames: dict) -> str:
    widths = metadata["glyphAdvanceWidths"]
    defaults = metadata["engravingDefaults"]

    missing = [name for name, _ in GLYPHS if name not in widths]

    if missing:
        raise SystemExit(f"metadata has no advance width for: {', '.join(missing)}")

    unnamed = [name for name, _ in GLYPHS if name not in glyphnames]

    if unnamed:
        raise SystemExit(f"SMuFL has no codepoint for: {', '.join(unnamed)}")

    missing_defaults = [name for name, _ in DEFAULTS if name not in defaults]

    if missing_defaults:
        raise SystemExit(
            f"metadata has no engraving default for: {', '.join(missing_defaults)}"
        )

    lines: list[str] = []
    out = lines.append

    out("#pragma once")
    out("")
    out("#include <cstddef>")
    out("")
    out("// Glyph metrics for the engraving layer, in staff spaces.")
    out("//")
    out("// GENERATED by tools/scripts/build/generate_glyph_metrics.py -- do not edit.")
    out("//")
    out(f"// Advance widths: {metadata['fontName']} {metadata['fontVersion']} metadata,")
    out(f"// {SOURCE_URL}")
    out("//")
    out("// Codepoints: the SMuFL specification, which every conforming font shares,")
    out(f"// {GLYPHNAMES_URL}")
    out("//")
    out("// Why a table rather than asking the font: the engraving layer is JUCE-free so")
    out("// that layout is assertable without a display, and a typeface is not. These are")
    out("// the widths layout needs to decide how much room a column takes; the font")
    out("// itself is only ever touched at paint time.")
    out("//")
    out("// Every value is in staff spaces, the unit SMuFL states its metrics in and the")
    out("// unit the layout works in, so nothing here is scaled on the way through.")
    out("namespace score::engraving")
    out("{")
    out("enum class Glyph")
    out("{")

    for _, identifier in GLYPHS:
        out(f"    {identifier},")

    out("};")
    out("")
    out("inline constexpr std::size_t glyphCount = " f"{len(GLYPHS)};")
    out("")
    out("// Advance width of each glyph, indexed by Glyph.")
    out("inline constexpr double glyphAdvanceWidths[glyphCount] = {")

    for name, identifier in GLYPHS:
        out(f"    {widths[name]:g}, // {identifier}")

    out("};")
    out("")
    out("[[nodiscard]] constexpr double advanceWidth(Glyph glyph) noexcept")
    out("{")
    out("    return glyphAdvanceWidths[static_cast<std::size_t>(glyph)];")
    out("}")
    out("")
    out("// SMuFL codepoint of each glyph, indexed by Glyph. These live in the Unicode")
    out("// private use area and are the same in every SMuFL-conforming font, which is")
    out("// what makes swapping Bravura for another one a build change and not a code")
    out("// change.")
    out("inline constexpr char32_t glyphCodepoints[glyphCount] = {")

    for name, identifier in GLYPHS:
        point = int(glyphnames[name]["codepoint"].removeprefix("U+"), 16)
        out(f"    U'\\u{point:04x}', // {identifier}")

    out("};")
    out("")
    out("[[nodiscard]] constexpr char32_t codepoint(Glyph glyph) noexcept")
    out("{")
    out("    return glyphCodepoints[static_cast<std::size_t>(glyph)];")
    out("}")
    out("")
    out("// SMuFL's engraving defaults for this font: the thicknesses and extensions a")
    out("// renderer is expected to take from the font rather than invent.")
    out("struct EngravingDefaults")
    out("{")

    for name, identifier in DEFAULTS:
        out(f"    double {identifier} = {defaults[name]:g};")

    out("};")
    out("")
    out("inline constexpr EngravingDefaults engravingDefaults{};")
    out("} // namespace score::engraving")

    return clang_format("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=pathlib.Path, help="path to Bravura.json")
    parser.add_argument(
        "glyphnames", type=pathlib.Path, help="path to SMuFL glyphnames.json"
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the committed header matches, and write nothing",
    )
    arguments = parser.parse_args()

    metadata = json.loads(arguments.metadata.read_text())
    glyphnames = json.loads(arguments.glyphnames.read_text())
    generated = render(metadata, glyphnames)

    if arguments.check:
        if not OUTPUT.exists():
            print(f"{OUTPUT} does not exist", file=sys.stderr)

            return 1

        if OUTPUT.read_text() != generated:
            print(f"{OUTPUT} is out of date; rerun without --check", file=sys.stderr)

            return 1

        print(f"{OUTPUT} is up to date")

        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(generated)
    print(f"wrote {OUTPUT} ({len(GLYPHS)} glyphs)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
