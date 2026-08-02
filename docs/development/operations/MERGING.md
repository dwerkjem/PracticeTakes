# Merging

This repository configures Git beyond the defaults. The settings exist because
a repository-wide restructure — moving `tests/` under `src/`, `scripts/` under
`tools/`, and renaming the C++ `src/services/` layer to `src/platform/` — had
to be carried onto nine branches at once, and the same conflicts came back on
every one of them.

Nothing here is required for correctness. A clone that skips it merges the same
commits to the same result, just with more conflicts to resolve by hand.

## Setup

```bash
pre-commit install                                  # once per clone
```

The `git-merge-config` hook installs the configuration on the next commit. To
do it immediately, or to check a clone:

```bash
python3 tools/scripts/git/configure_merge.py
python3 tools/scripts/git/configure_merge.py --check
```

Git refuses to read merge-driver definitions out of a cloned repository — a
repository that could define the command Git runs during a merge would be a
remote code execution vector. So `.gitattributes` can *name* a driver but not
define one, and every clone has to opt in locally. That is the only reason this
is a script rather than a committed config file.

## What is configured

| Setting | Why |
| --- | --- |
| `merge.conflictStyle=zdiff3` | Puts the common ancestor inside the conflict. In a rename-heavy merge, two versions without the base leave no way to tell which side moved a line. |
| `merge.directoryRenames=true` | Follows a directory rename onto files the other branch added inside it. Without it, a branch adding `tests/foo/BarTests.cpp` while the base moves `tests/` to `src/tests/` leaves the file stranded at the old path. |
| `merge.renameLimit`, `diff.renameLimit` | Rename detection gives up past a candidate count and degrades to add-plus-delete. The defaults sit well below the size of a repository-wide move, which is exactly when rename detection matters most. |
| `rerere.enabled`, `rerere.autoUpdate` | Records a conflict resolution and replays it the next time the same conflict appears. Carrying one change across a fleet of branches otherwise means resolving the identical conflict once per branch. |

## Merge drivers

Declared in `.gitattributes`, defined by `configure_merge.py`.

### `cmake-sources` — `CMakeLists.txt`

The source lists inside `target_sources(...)` and `add_executable(...)` are
unordered sets of paths. Any two branches that add or move a file collide in
them, and the resolution is always the same: keep every path both sides still
want.

The driver runs the ordinary three-way merge first, then inspects what
conflicted. **A conflict region is resolved only if every line on all three
sides is a bare source path.** A changed `set()`, two different `if()` bodies,
a rewritten comment, a generator expression, a `${VARIABLE}` entry, or a path
with a trailing comment all disqualify the region, which stays conflicted for a
human.

That restraint is the point. A build file that merges wrongly still parses; the
mistake surfaces much later as a confusing link error or a silently dropped
translation unit. Deletions are honoured — a path removed on either side does
not come back. A list that was sorted stays sorted; one that was grouped keeps
its grouping, with additions appended.

### `npm-lock` — `src/services/package-lock.json`

A lockfile is generated, so merging it line by line is meaningless. The driver
starts from the incoming version and runs `npm install --package-lock-only`,
which rewrites the tree from the merged manifest without touching
`node_modules`.

It refuses rather than guesses. Missing `npm`, a `package.json` that is itself
still conflicted, a failed install, or output that is not valid JSON all exit
non-zero and leave an ordinary Git conflict — so an offline clone cannot
silently commit a half-resolved lockfile. A failed run restores the lockfile
that was in the working tree.

### `ours` — generated `.ua/` files

`knowledge-graph.json` and its siblings are tens of thousands of lines written
by `/understand`. A textual merge of two versions produces valid JSON
describing a repository that never existed, which is worse than either input.
Keep ours and regenerate.

### `union` — `docs/development/quality/manual-runs/*.md`

Verification evidence is an append-only record of runs that actually happened.
Two branches recording different runs is not a conflict; both are true.

## Escape hatches

A driver is a convenience, never an authority. To bypass one for a single
merge:

```bash
git merge -X no-renormalize <branch>          # skip attribute-driven merging
git checkout --ours CMakeLists.txt            # or --theirs, then edit
```

To remove the configuration entirely:

```bash
git config --local --remove-section merge.cmake-sources
git config --local --remove-section merge.npm-lock
```

The drivers have unit tests under `tools/scripts/git/`, run by
`python3 tools/scripts/run_tests.py`. If a driver ever produces a wrong
resolution, that is a bug with a missing test — add the case before fixing it.

## Reviewing a merge

`rerere` replaying a resolution and `cmake-sources` unioning a list both mean a
merge commit can contain changes nobody looked at in that merge. Before pushing
one that touched `CMakeLists.txt`, confirm the build still configures:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --target PracticeTakesTests --parallel
ctest --test-dir build --output-on-failure
```

A source list that lost an entry fails to link; one that gained a stale entry
fails to configure. Both are loud, which is why unioning them is safe and
unioning arbitrary code is not.
