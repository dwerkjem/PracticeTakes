# Embedded typefaces

Two faces, embedded for the same reason and used for different things: the
interface is drawn in **IBM Plex Sans**, and notation in **Bravura**.

## UI typeface

Practice Takes draws its interface in **IBM Plex Sans**, embedded in the
executable rather than requested from the platform by name.

- `IBMPlexSans-Regular.ttf`
- `IBMPlexSans-Bold.ttf`

Both are version 3.005, taken unmodified from
[github.com/IBM/plex](https://github.com/IBM/plex) at
`packages/plex-sans/fonts/complete/ttf/`.

### Why embedded rather than named

`setDefaultSansSerifTypefaceName("IBM Plex Sans")` would be one line and no
files. It also works only on a machine where somebody installed the font, and
falls back to the platform sans everywhere else — silently. Nothing fails,
nothing logs, and the application simply looks like a different application.
The first symptom would be a screenshot comparison months later.

`CMakeLists.txt` turns these two files into a `PracticeTakesFonts` binary-data
target, the same mechanism the application icon already uses, and
`src/application/theme/AppLookAndFeel.cpp` hands the parsed faces to JUCE.
`src/tests/application/theme/AppLookAndFeelTests.cpp` asserts the loaded faces
report `IBM Plex Sans`, so a fallback fails the suite instead of merely looking
wrong.

### Why only two weights

The design system this came from uses weights 400, 500, 600 and 700. JUCE
resolves a font to plain or bold, so 500 and 600 have nowhere to land without a
name-keyed lookup that nothing in the application currently asks for. Regular
and Bold are the honest mapping. Adding Medium and SemiBold would be roughly
another 400 KB for weights no caller names today.

### Licence

IBM Plex Sans is licensed under the SIL Open Font License 1.1, reproduced in
full in `OFL.txt`. The licence permits bundling the font in a larger work; it
requires the licence to travel with the font, which is why `OFL.txt` sits beside
the files rather than being summarised somewhere.

The fonts are shipped as complete, unmodified font files. This differs from the
application icon, which reproduces only the *outlines* of two IBM Plex Mono
glyphs as path data specifically to avoid vendoring a font file — see
[`../icons/README.md`](../icons/README.md). That choice was right for artwork
built once at design time; a typeface the application renders with at runtime
has to actually be present.

### Updating

Replace the files from the same upstream path, keep `OFL.txt` in step, and run
`ctest --test-dir build -R typeface`. If IBM ever renames the family, that test
is what will tell you.

## Music typeface

Notation is drawn in **Bravura**, the reference font for
[SMuFL](https://w3c.github.io/smufl/) (the Standard Music Font Layout).

- `Bravura.otf`

Version 1.482, taken unmodified from
[github.com/steinbergmedia/bravura](https://github.com/steinbergmedia/bravura)
at `redist/otf/Bravura.otf`.

### Why embedded, even harder than for the UI face

The UI face has a fallback: a machine without IBM Plex draws the interface in
the platform sans and merely looks wrong. Notation has none. SMuFL glyphs live
in the Unicode private use area, so a machine without Bravura draws a page of
empty boxes, and there is no system music font anywhere to land on instead.

`src/tests/features/score/notation/MusicFontTests.cpp` asserts the loaded face
reports `Bravura`, so a face that failed to parse fails the suite rather than
shipping a blank page.

### Why the metadata is not here

Bravura ships `redist/Bravura.json`: 1.25 MB of metrics for 3467 glyphs. The
renderer draws about thirty. Committing the rest to carry those would be
absurd, so the JSON is **not** vendored — `tools/scripts/build/generate_glyph_metrics.py`
fetches it, extracts what the layout needs, and writes
`src/platform/score/engraving/GlyphMetrics.h`, which *is* committed. The
header's banner records the font version and both source URLs, so the table can
always be traced back.

The codepoints in that table come from SMuFL's own `glyphnames.json` rather than
from Bravura, because they are the specification's and shared by every
conforming font. That is what makes replacing Bravura with Petaluma or Leland a
change to this directory and to `CMakeLists.txt`, and not a change to any
rendering code.

### Licence

Bravura is licensed under the SIL Open Font License 1.1, reproduced in full in
`OFL-Bravura.txt` — a separate file from `OFL.txt` because it is a separate
copyright holder (Steinberg Media Technologies GmbH, with the Reserved Font
Name "Bravura"). Both licences travel with their own font.

### Updating

Replace `Bravura.otf` from the same upstream path, keep `OFL-Bravura.txt` in
step, then **regenerate the metrics table** — the widths are version-specific:

```bash
curl -o /tmp/Bravura.json \
    https://raw.githubusercontent.com/steinbergmedia/bravura/master/redist/Bravura.json
curl -o /tmp/glyphnames.json \
    https://raw.githubusercontent.com/w3c/smufl/gh-pages/metadata/glyphnames.json
python3 tools/scripts/build/generate_glyph_metrics.py /tmp/Bravura.json /tmp/glyphnames.json
```

`--check` verifies the committed header matches without writing. Run the
notation tests afterwards: they pin two advance widths and two codepoints
deliberately, so a silent change to every column width in the application fails
there instead.
