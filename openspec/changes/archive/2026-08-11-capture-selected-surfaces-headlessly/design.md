## Context

Three facts, from the code rather than assumed.

**Capture has no surface filter.** `test-suite capture` takes `--mode`,
`--resolutions`, `--themes`, `--window-title`, and `--run`. The plan comes from
`surfaces.plan(mode, resolutions, themes)`, which returns the cross product of
`surfaces_for_mode(mode)` with the configured geometries and palettes. Nothing
narrows the first term.

**Review already has one.** `ui-capture-and-review` requires that a review can
be narrowed by palette, resolution, area, presentation, tools, and tags. So the
idea is established for looking at captures; it is only missing from making
them.

**Capture is already unattended, but not unobtrusive.** `capture.py` says
outright that it does not park the pointer, because an X capture of a window
does not include the cursor. What it cannot avoid is the windows themselves:
`ApplicationDriver` launches the application with the inherited environment, so
it lands on `DISPLAY` — the operator's screen — and is resized once per
geometry per surface.

## Goals / Non-Goals

**Goals**

- Seeing one surface after changing it costs one command and no waiting through
  the others.
- A capture run can happen while the machine is being used for something else.
- A mistyped surface name fails loudly before the run rather than producing an
  empty one.

**Non-Goals**

- Changing what a capture is, or the store, or the review grid.
- A headless `attend`. That pass is a person looking at a live application.
- A CI capture leg. This removes one obstacle to it; it is not it.

## Decisions

### 1. Select by approved-state name, not by a new identifier

A `Surface` already carries `state`, the approved window state the harness puts
the application into, and the harness already validates every one of them
against `list-states` before a run. Selecting by that name means no new
vocabulary, and the name a person reads in the review grid is the name they type.

*Alternative rejected:* selecting by surface title. Titles are prose, they change
when wording improves, and they are not unique in any enforced way.

### 2. An unknown name fails the run

The tempting behaviour is to warn and carry on with whatever matched. That
produces a run that captured less than the operator believes, and the difference
between "I captured the tuner and it is fine" and "I captured nothing and saw no
failures" is invisible in the output.

Failing before the run costs a retype and cannot be misread.

### 3. Xvfb, and DISPLAY set on the process environment

`xwindow_capture` reads pixels from the X server, so a screen with no monitor
produces identical images. Xvfb is the standard way to get one, is packaged
everywhere this project builds, and needs no privileges to run.

The display is published by setting `os.environ["DISPLAY"]` for the duration
rather than threading an environment dict through `ApplicationDriver` and each
tool invocation. Every subprocess the run starts inherits it, so nothing else in
the testing suite has to know a virtual display exists. The previous value is
restored on the way out, including when the body raises.

*Alternative rejected:* `xvfb-run`. It wraps a single command, and the capture
pass is a Python process that starts several — the application, the capture
tool, and the build of the capture tool — over a run's lifetime.

### 4. Refuse rather than fall back when Xvfb is missing

Falling back to the desktop display would put windows on the screen of somebody
who asked for headless precisely so that would not happen, and they would find
out by watching it happen. The error names the install command.

### 5. A 1920x1200 virtual screen

`maximised` is implemented as `window->setBounds(display->userArea)` — the
geometry is defined by the screen. On a virtual screen the size of the ordinary
window, the widest capture would quietly become the narrowest, and the image
would look plausible while testing the wrong thing. 1920x1200 is comfortably
above the 1280x800 default.

### 6. Display numbers start at :90

`:0` and the low numbers are where real sessions live. The search takes the
lowest free number in a private range and checks both the socket file and a
connection to it, so a stale socket from a crashed server is not mistaken for a
free number — which would otherwise mean capturing somebody else's screen.

### 7. Find the window without a window manager — recorded 2026-08-11

The first headless run captured nothing: `the window never settled at a size`.
Not a timing problem. `window_control` and `xwindow_capture` both located the
application's window through `_NET_CLIENT_LIST`, which is published by the
*window manager*. Xvfb has none, so the list does not exist, no window is ever
found, the size poll returns nothing until it times out, and the failure names
the symptom rather than the cause.

The obvious fix is to run a small window manager inside Xvfb. Rejected: it is a
second package to install for something the tools should not need. `_NET_WM_PID`
is set by the application, not by the window manager, so a walk of the window
tree matching that property works with or without one.

The client list stays as the first attempt, because where there *is* a window
manager it is authoritative about which windows are top-level. The tree walk is
the fallback, and it takes the largest viewable window belonging to the process:
a toolkit makes several X windows per process, and with no window manager to say
which is real, size is what separates the one a person would see from the ones
they would not.

Both tools carried byte-identical copies of this lookup. Fixing it in two places
that must agree is how they stop agreeing, so it moved to `x_window_lookup.h`
and both now include it.

## Risks / Trade-offs

- **Xvfb rendering is not desktop rendering.** Font hinting and compositing can
  differ, so a headless capture is not evidence about how the application looks
  on real hardware. Mitigated by keeping the desktop path the default and saying
  so in the documentation; the images are for judging layout and state, which is
  what the surfaces are about.
- **A new dependency for a convenience.** Xvfb is only needed by `--headless`,
  and its absence is a clear message rather than a failure.
- **Selection makes partial runs easy.** A run covering two surfaces is not a
  verification pass, and the run record will say so by its contents. The export
  and release gate already read runs rather than assuming completeness.

## Migration Plan

1. `plan()` takes an optional state set. Pure function, tested directly.
2. `display.py` and its tests, which do not require Xvfb to run.
3. Both options on `capture`, and the images path in the summary.
4. xvfb in the dependency script; documentation.

## Open Questions

- **Should `--headless` become the default once it is proven?** Arguments both
  ways: it is strictly less disruptive, and it is also one step further from
  what a user's machine actually renders. Left as an opt-in until there is
  experience with it.
