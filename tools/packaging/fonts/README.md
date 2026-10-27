# Typefaces

Practice Takes draws its interface in **IBM Plex Sans** and its notation in
**Bravura**, both embedded in the executable rather than requested from the
platform by name. Plex is described first; Bravura has a section of its own at
the end.

## UI typeface

- `IBMPlexSans-Regular.ttf`
- `IBMPlexSans-Bold.ttf`

Both are version 3.005, taken unmodified from
[github.com/IBM/plex](https://github.com/IBM/plex) at
`packages/plex-sans/fonts/complete/ttf/`.

## Why embedded rather than named

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

## Why only two weights

The design system this came from uses weights 400, 500, 600 and 700. JUCE
resolves a font to plain or bold, so 500 and 600 have nowhere to land without a
name-keyed lookup that nothing in the application currently asks for. Regular
and Bold are the honest mapping. Adding Medium and SemiBold would be roughly
another 400 KB for weights no caller names today.

## Licence

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

## Updating

Replace the files from the same upstream path, keep `OFL.txt` in step, and run
`ctest --test-dir build -R typeface`. If IBM ever renames the family, that test
is what will tell you.

## Music font

- `Bravura.otf`
- `Bravura-OFL.txt`

Bravura version 1.482, taken unmodified from
[github.com/steinbergmedia/bravura](https://github.com/steinbergmedia/bravura)
at `redist/otf/Bravura.otf` (SHA-256
`cdf0f893ee1fdb64b7f6713d71ee0dcfc349c0ac01429a8e451b01a9e79f5f3b`), with the
repository's `LICENSE.txt` beside it as `Bravura-OFL.txt`.

Bravura is Steinberg's reference font for SMuFL, the Standard Music Font Layout,
which fixes a codepoint for every notation symbol and fixes one em at four staff
spaces. The score view's layout already measured in staff spaces, so the font
dropped in without the layout changing scale. `src/features/score/MusicFont`
loads it and `src/features/score/SmuflGlyphs` names the glyphs drawn.

### Why embedded

The same reason as Plex, and stronger. A missing UI face falls back to the
platform sans; a missing music font draws a Private Use Area codepoint as
nothing. `src/tests/features/score/MusicFontTests.cpp` asserts the family, that
every glyph the renderer asks for is present, and that a notehead measures the
1.18 by 1.0 staff spaces Bravura publishes, which is the check that the size
conversion is right.

### Why the whole font

Bravura holds some 3,000 glyphs and the renderer uses about fifty, so a subset
would be a few percent of the ~870 KB this file adds. It is shipped whole
anyway, because of the licence: Bravura has a **Reserved Font Name**, and under
the OFL a subset is a Modified Version, which may not use that name. A subset
would have to be renamed, and its provenance explained. The whole file needs
neither.

### Licence

SIL Open Font License 1.1, with Reserved Font Name "Bravura", reproduced in full
in `Bravura-OFL.txt`. The same licence text is also inside the font's own name
table.

### Updating

Replace `Bravura.otf` from the same upstream path, update the version and hash
above, and run `ctest --test-dir build -R typeface`. A new release that moves a
glyph's origin or size fails "glyphs keep SMuFL's origins" or "one em of the
music font is four staff spaces", and if the notehead width changes, "the
layout's notehead is the font's notehead" fails too.
