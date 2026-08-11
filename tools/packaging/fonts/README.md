# UI typeface

Practice Takes draws its interface in **IBM Plex Sans**, embedded in the
executable rather than requested from the platform by name.

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
