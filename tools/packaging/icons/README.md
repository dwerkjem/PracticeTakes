# Application icon

The Practice Takes mark: a "PT" monogram in IBM Plex Mono SemiBold over two
faint staff systems, drawn from the application's own accent colour
(`#64AAFF` dark, `#3770C4` light — see `src/application/theme/Theme.cpp`).

Every file here is generated. Edit the geometry in
[`tools/scripts/design/generate_app_icons.py`](../../scripts/design/generate_app_icons.py)
rather than the artwork, then regenerate:

```bash
uv run --no-project --with pillow tools/scripts/design/generate_app_icons.py
```

Pillow is the only dependency and is not needed to build or test the
application — the generated files are committed.

## Files

| File | Used by |
| --- | --- |
| `practice-takes-1024.png` | JUCE `ICON_BIG`; the largest hicolor size |
| `practice-takes-128.png` | JUCE `ICON_SMALL`; embedded and published as `_NET_WM_ICON` at runtime |
| `practice-takes-{32,48,64,256,512}.png` | Freedesktop hicolor theme |
| `practice-takes.svg` | Freedesktop `hicolor/scalable` |
| `practice-takes.ico` | NSIS installer branding on Windows |
| `practice-takes-light{,-512,-1024}.{svg,png}` | Alternate mark for light backgrounds; not installed |

JUCE turns `ICON_BIG`/`ICON_SMALL` into the macOS `.icns` and the Windows
executable icon, but ignores them on Linux, where window managers read the icon
from `_NET_WM_ICON` and launchers read it from the `.desktop` entry. Both paths
are wired up: see `CMakeLists.txt`, `tools/cmake/Packaging.cmake`, and
`src/bootstrap/main.cpp`.

A 256x256 image is too large for `_NET_WM_ICON` — the property exceeds the X11
maximum request size and silently arrives empty — so the runtime icon is the
128x128 one.

## Source

The artwork comes from the `ICO.html`, `ICO-light.html`, and `thumbnail.html`
components of the Claude Design "Design System" project, which are CSS layouts
on a 1024x1024 canvas. The generator restates that geometry so neither a
browser nor a font file is needed to rebuild it. The one intentional
difference: the source centres the text *element*, whose trailing letter space
pushes the glyphs about 23px right of centre; the generator centres the glyphs
themselves.

IBM Plex Mono is licensed under the SIL Open Font License 1.1. Only the
outlines of "P" and "T" are reproduced, as path data.
