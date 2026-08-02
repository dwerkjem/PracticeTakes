# SOPS secret management

Practice Takes stores local plaintext secrets outside Git and commits only
SOPS-encrypted mirrors. The repository-level `tools/secret-patterns` file determines
which plaintext files are managed. Encrypted files are written below
`.secrets/` while preserving the original relative path:

```text
src/services/feedback-intake/.dev.vars
    -> .secrets/services/feedback-intake/.dev.vars.sops
```

## One-time setup

Install [SOPS](https://getsops.io/docs/install/) and
[age](https://github.com/FiloSottile/age). Generate an age identity if you do
not already have one:

```bash
mkdir -p ~/.config/sops/age
age-keygen -o ~/.config/sops/age/keys.txt
```

Keep the `AGE-SECRET-KEY-...` identity private. Use the printed `age1...`
public recipient to configure this repository:

```bash
python3 tools/scripts/secrets/secrets_manager.py init --age-recipient age1...
```

Then install the Git hooks. **Every clone needs this step.** The hook is the
only local control that keeps plaintext secrets out of commits, and a clone
without it has no protection at all:

```bash
pre-commit install
```

`pre-commit install` honors `default_install_hook_types` in
`.pre-commit-config.yaml`, so the read-only `sops-secret-audit` hook also runs
on merge commits, which Git otherwise creates without running any pre-commit
hook. Encryption deliberately stays on ordinary commits: a merge commit must
keep the mirrors it is merging in, not overwrite them with whatever plaintext
happens to be on disk. Run `sync` after a merge that touched `.secrets/`.

The hooks remain defense in depth rather than a guarantee: `git commit
--no-verify` skips them, and they match file paths, not file contents, so a
credential pasted into source or documentation is never detected.

The public recipient in `.sops.yaml` is safe to commit. Each collaborator who
needs access must have a corresponding authorized identity.

## Key backup and rotation

`.sops.yaml` lists a single age recipient today, so exactly one identity can
read every mirror. Back that identity up before anything else: copy
`~/.config/sops/age/keys.txt` into a password manager or onto offline media.
Losing it makes every committed mirror unreadable. The mirrors also live in a
public repository, so anyone who obtains the identity can decrypt every version
of every mirror that Git has ever stored.

Adding a second recipient — an offline escrow identity, or one identity per
collaborator — removes that single point of failure. Whether to do so is a
project decision, because it means generating and safely storing another key.
List the recipients comma separated:

```yaml
creation_rules:
  - path_regex: ^\.secrets/.*\.sops$
    age: >-
      age1<primary>,
      age1<backup>
```

Then re-wrap the data key of each existing mirror:

```bash
sops updatekeys --yes .secrets/services/feedback-intake/.env.sops
sops updatekeys --yes .secrets/services/feedback-intake/.dev.vars.sops
sops updatekeys --yes .secrets/services/feedback-intake/wrangler.jsonc.sops
```

`updatekeys` only re-wraps the existing data key for the current recipient list.
It never generates a new data key, so a recipient that was removed can still
decrypt any ciphertext it saw earlier. Generating a fresh data key is a separate
step:

```bash
sops rotate --in-place --input-type json --output-type json \
  .secrets/services/feedback-intake/.env.sops
```

Rotate when an identity is lost or exposed, when a collaborator leaves, and at
least once a year otherwise:

1. Generate the replacement identity with `age-keygen` and back it up.
2. Add the new `age1...` recipient to `.sops.yaml` next to the old one and run
   `sops updatekeys` on every file below `.secrets/`.
3. Verify the new identity alone can read the mirrors, without touching the
   working tree: `SOPS_AGE_KEY_FILE=/path/to/new-keys.txt sops decrypt
   --input-type json --output-type binary
   .secrets/services/feedback-intake/.env.sops > /dev/null`.
4. Remove the old recipient from `.sops.yaml`, run `sops updatekeys` again, then
   `sops rotate --in-place` on every mirror so the data keys are new as well.
5. Commit the re-keyed mirrors together with `.sops.yaml`.
6. If the old identity was exposed rather than merely retired, also rotate the
   credentials the mirrors carry: `CLOUDFLARE_API_TOKEN`, `ADMIN_PASSWORD`, and
   `SUBMISSION_SIGNING_KEY`. Git history keeps every earlier ciphertext, and the
   exposed identity still decrypts those, so re-keying revokes nothing on its
   own.

## Daily commands

Encrypt local files and stage only their encrypted mirrors:

```bash
python tools/scripts/secrets/secrets_manager.py encrypt
```

Restore plaintext after cloning:

```bash
python tools/scripts/secrets/secrets_manager.py decrypt
```

Safely synchronize both directions:

```bash
python tools/scripts/secrets/secrets_manager.py sync
```

The sync command records plaintext hashes only in `.git/sops-secret-state.json`;
they are never committed. It can therefore distinguish a local-only edit from
an encrypted-only edit. If both sides changed, it stops and writes two
local-only comparison files below `.git/sops-secret-conflicts/`. Review them,
then explicitly choose:

```bash
python tools/scripts/secrets/secrets_manager.py sync --prefer-local
python tools/scripts/secrets/secrets_manager.py sync --prefer-encrypted
```

`--prefer-encrypted` overwrites the local plaintext. Use it only after
reviewing the saved comparison.

## Git conflict resolution

Do not edit conflicted `.sops` ciphertext manually. Decrypt and three-way merge
the Git stages with:

```bash
python tools/scripts/secrets/secrets_manager.py resolve
```

Clean plaintext merges are re-encrypted and staged automatically. If Git
cannot merge the plaintext cleanly, the manager writes a local-only `.merge`
file below `.git/sops-secret-conflicts/`. Edit that file, remove all conflict
markers, and continue:

```bash
python tools/scripts/secrets/secrets_manager.py resolve --continue
```

To choose one side without merging:

```bash
python tools/scripts/secrets/secrets_manager.py resolve --ours
python tools/scripts/secrets/secrets_manager.py resolve --theirs
```

`.gitattributes` marks `.secrets/` as binary and disables Git's ciphertext
merge attempt, ensuring the manager receives intact base, ours, and theirs
stages.

## Safety behavior

The `sops-secrets` hook:

1. Reads the ordered globs in `tools/secret-patterns`.
2. Mirrors those patterns into the clone-local `.git/info/exclude`.
3. Removes newly added matching plaintext files from the index.
4. Encrypts each matching plaintext file as binary SOPS JSON.
5. Stages only changed files below `.secrets/`.
6. Refuses to commit if a matching plaintext file was already tracked or an
   encrypted secret still has a Git conflict.

The `sops-secret-audit` hook then scans the whole index, not just the staged
change, and fails if any tracked path matches `tools/secret-patterns`. It catches
plaintext that reached the repository through a bypassed hook or a merge. Run
the same check by hand, or from CI, with:

```bash
python3 tools/scripts/secrets/secrets_manager.py audit
```

`tools/secret-patterns` selects `.env`, `.dev.vars`, `*.secret`, `*.secrets`, the
deployed `src/services/feedback-intake/wrangler.jsonc`, and everything below the
repository-root `secrets/` directory. Example and template files stay plaintext.

Every decrypted copy the manager writes outside the working tree — sync
comparisons, merge scratch files, and manual `.merge` files — lives below
`.git/sops-secret-conflicts/` with mode 0600, and is deleted once the conflict
it describes is resolved.

If a real secret was ever committed in plaintext, removing the file is not
enough: rotate the credential immediately and separately clean the Git history.
