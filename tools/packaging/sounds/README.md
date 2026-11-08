# Sounds

Practice Takes plays the open score through the practice clock
(`openspec/changes/archive/2026-11-08-score-playback`). Two pieces of other people's work make
that possible. Neither is committed here: `tools/cmake/ScorePlayback.cmake`
downloads each from its exact commit, at configure time, and checks it against
its SHA-256.

| What | Where from | Licence | Shipped as |
|---|---|---|---|
| **TinySoundFont** (`tsf.h`), the SoundFont player | [schellingb/TinySoundFont](https://github.com/schellingb/TinySoundFont) at `853a0a1` | MIT | compiled in; `TinySoundFont-LICENSE.txt` beside the SoundFont |
| **GeneralUser GS v2.0.3**, a 32 MB General MIDI bank by S. Christian Collins | [mrbumpy409/GeneralUser-GS](https://github.com/mrbumpy409/GeneralUser-GS) at `684543d` | GeneralUser GS License v2.0 | `GeneralUser-GS.sf2` and `GeneralUser-GS-LICENSE.txt` |

## Where the SoundFont is installed

The shell looks for `sounds/GeneralUser-GS.sf2` in each place a platform
installs it, relative to the executable:

| Build | Location |
|---|---|
| A build tree, and Windows | `bin/sounds/`, beside the executable |
| Linux packages | `/usr/share/practice-takes/sounds/` |
| macOS | `Practice Takes.app/Contents/Resources/sounds/` |

Without it the clock works as before, and the Mix popup says that playback is
unavailable.

## Why these

- **TinySoundFont** is one header, reads SF2, and is MIT. Read with care, it
  allocates only on loading, when a voice is needed and no maximum is set, and
  when a channel is used for the first time. All three are moved off the audio
  thread (design decision 1).
- **GeneralUser GS** is the best-known General MIDI bank that a program may
  ship. Its licence says: *"Please feel free to use it in your software
  projects, and to modify the SoundFont bank or its packaging to suit your
  needs."* At 32 MB it is a quarter the size of FluidR3_GM, the other
  permissive candidate.

## For the release review

GeneralUser GS's licence carries a caveat worth reading before a release. Some
of its samples came from other free banks, and the author cannot vouch for every
sample's origin. He has had no complaint since 2000, and many commercial
products ship it. The maintainer decides whether that is acceptable for a
release; it is not settled here.

The author also asks that nobody link straight to his download files. The build
downloads from his GitHub repository by commit, not from his website.
