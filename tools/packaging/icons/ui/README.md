# Interface icons

The small icons beside Practice Takes' settings, menus and buttons, so a control
can be found by its picture as well as its words -- including by someone running
the application in a language they do not read, looking for the globe.

They are [Lucide](https://lucide.dev) icons, copied unchanged from
`lucide-static` **0.460.0** (`icons/<name>.svg`), under Lucide's ISC licence
(`LICENSE.txt`, which must travel with them). Each is a 24×24 stroked outline in
`currentColor`; `src/application/theme/Icons.cpp` substitutes the colour of the
text beside it before JUCE parses the SVG.

CMake embeds every `*.svg` here as `PracticeTakesUiIcons`, by file name
(`globe.svg` as `globe_svg`). To add one: copy it from the same version of
`lucide-static`, add an `appearance::Icon` and its file name in `Icons.cpp`, and
use it. An icon nothing uses should be deleted rather than kept.
