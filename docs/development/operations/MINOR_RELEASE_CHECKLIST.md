# Minor release checklist

A minor release (`0.6.0` → `0.7.0`) ships compatible new functionality. The
mechanics are in [Releasing](RELEASING.md); this page is the order to do them
in, and the checks that happen *around* the workflow, which the workflow cannot
make for you. Copy the list into the release's tracking issue, or the stage
issue it closes, and tick it there.

A patch release can skip the roadmap and wiki sections. A major release needs
everything here and a decision recorded in the roadmap issue (#10) about what
compatibility it breaks.

## 1. Decide what is in it

- [ ] The stage issue the release closes (the release's `tracker` in
      `tools/scripts/planning/roadmap_ledger.json`) has no open child issue
      that blocks it. Anything not shipping is moved to a later stage, with
      its ledger entry changed in the same pull request.
- [ ] Every `status:ready-for-human` issue that belongs to the release has had
      its hardware session: a real microphone, MIDI keyboard, sustain pedal or
      speakers, whatever it names. Close each one as *completed*, or file what
      it found.
- [ ] No open pull request is meant to be in the release. Merge it or say it
      waits for the next one.
- [ ] No OpenSpec change in `openspec/changes/` is half-built in a way users
      would see. A change whose last task is done is archived.
- [ ] It really is a minor release: nothing breaks compatibility. In
      particular, a `.ptsettings` file exported from the previous release
      still imports, and its saved workspace still restores. If
      `schemaVersion` in `src/application/configuration/AppDefaults.h` moved,
      the migration from the old value is tested.

## 2. Make sure `main` is green

- [ ] The latest commit on `main` passes every workflow: desktop builds,
      smoke tests, coverage, sanitizers, benchmarks, Python and services
      checks, and secret scan.
- [ ] The Clang-Tidy auto-fix on `main` has finished. A release queues behind
      it anyway, but its commit changes `src/`, which invalidates a manual run
      taken before it (step 3).
- [ ] The local suites pass on the commit you will release:

  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
  cmake --build build --target PracticeTakesTests --parallel
  ctest --test-dir build --output-on-failure
  ```

  ```bash
  python tools/scripts/run_tests.py
  ```

  ```bash
  cd src/services && npm ci && npm run check && npm run test
  ```

- [ ] `docs/contracts/testcontrol/approved-states.json` is current: building
      `PracticeTakesTests` leaves it unchanged.

## 3. Verify the build by hand

The release workflow refuses to build without a complete, current, full-mode
run. Do this last, after the final code change: anything under `src/`,
`CMakeLists.txt`, `tools/cmake/`, `tools/packaging/` or `tools/vcpkg.json` that
lands afterwards invalidates it.

- [ ] Reference images that a deliberate visual change made stale are retaken
      and approved first, as their own issues say.
- [ ] Full capture of `main`:

  ```bash
  uv run test-suite capture --mode full
  ```

- [ ] Every capture in the grid reviewed and answered.
- [ ] The attended pass, at the machine, against a live application with a
      real input device:

  ```bash
  uv run test-suite attend
  ```

- [ ] Any failure is fixed, or waived with a note saying why it does not block
      the release.
- [ ] Record exported and run history synced:

  ```bash
  uv run test-suite export
  ```

  ```bash
  uv run test-suite sync
  ```

- [ ] The record is committed to `main` in its own pull request ("Record the
      X.Y.0 manual verification run"), and nothing release-affecting merges
      after it.
- [ ] The gate passes locally on `main`:

  ```bash
  python3 tools/scripts/release/check_manual_verification.py
  ```

Skipping the gate is possible (the workflow's `skip_manual_verification`
input, with a reason) and is recorded with the release. It is for an
emergency patch, not a minor release.

## 4. Make the user-facing words true

- [ ] `docs/wiki/` describes what ships: `Features.md` marks each feature's
      status, `Tools.md` lists every tool, `Roadmap.md` shows the stage as
      released. Pull requests should have kept this current; check it rather
      than assume it.
- [ ] The root `README.md` does not describe the application as it was two
      releases ago.
- [ ] Anything new that is shipped from someone else's work (a library, a
      SoundFont, a font) has its licence installed with it, and is listed in
      its `tools/packaging/` README.

## 5. Release

- [ ] Confirm the version it will produce:

  ```bash
  python3 tools/scripts/release/version.py next minor
  ```

- [ ] **Actions → Create Practice Takes release → Run workflow**, `minor`,
      with the manual-verification skip left off.
- [ ] The manual verification gate passes. If it fails, it names the files
      that changed since the run: re-run step 3; do not skip.
- [ ] All six packages build and the bundle verifies. If a job fails, use
      **Re-run failed jobs** on the same run so it keeps the same tag. Never
      move or reuse a published tag; fix forward in a patch release.

## 6. Check what was published

- [ ] The GitHub Release `vX.Y.0` has six packages, `SHA256SUMS.txt` and
      `BUILD-METADATA.txt`, and `BUILD-METADATA.txt` names the expected commit.
- [ ] Provenance verifies for a downloaded package:

  ```bash
  gh attestation verify PracticeTakes-X.Y.0-linux-x64.deb --repo dwerkjem/PracticeTakes
  ```

- [ ] Install from the published package on at least one platform, with no
      build tree on the machine: it starts, the window title shows `X.Y.0`,
      it hears the microphone, and anything the package must carry beside the
      executable works (score playback reads the installed SoundFont).
- [ ] The release notes read sensibly: edit the generated text if a headline
      feature is buried in a list of pull requests.

## 7. Close the stage

- [ ] The stage issue's exit criteria are ticked and it is closed as
      *completed*, so the roadmap forecast credits it.
- [ ] The milestone has no open issues, and is closed. Its due date comes from
      the ledger; leave it, and leave its title.
- [ ] The roadmap issue's Live forecast has rerun and still makes sense for
      the next stage.
