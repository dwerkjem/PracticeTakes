# SOPS secret management

Practice Takes stores local plaintext secrets outside Git and commits only
SOPS-encrypted mirrors. The repository-level `secret-patterns` file determines
which plaintext files are managed. Encrypted files are written below
`.secrets/` while preserving the original relative path:

```text
services/feedback-intake/.dev.vars
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
python3 scripts/secrets/secrets_manager.py init --age-recipient age1...
pre-commit install
```

The public recipient in `.sops.yaml` is safe to commit. Each collaborator who
needs access must have a corresponding authorized identity. After changing
recipients in `.sops.yaml`, use `sops updatekeys` on the encrypted mirrors.

## Daily commands

Encrypt local files and stage only their encrypted mirrors:

```bash
python scripts/secrets/secrets_manager.py encrypt
```

Restore plaintext after cloning:

```bash
python scripts/secrets/secrets_manager.py decrypt
```

Safely synchronize both directions:

```bash
python scripts/secrets/secrets_manager.py sync
```

The sync command records plaintext hashes only in `.git/sops-secret-state.json`;
they are never committed. It can therefore distinguish a local-only edit from
an encrypted-only edit. If both sides changed, it stops and writes two
local-only comparison files below `.git/sops-secret-conflicts/`. Review them,
then explicitly choose:

```bash
python scripts/secrets/secrets_manager.py sync --prefer-local
python scripts/secrets/secrets_manager.py sync --prefer-encrypted
```

`--prefer-encrypted` overwrites the local plaintext. Use it only after
reviewing the saved comparison.

## Git conflict resolution

Do not edit conflicted `.sops` ciphertext manually. Decrypt and three-way merge
the Git stages with:

```bash
python scripts/secrets/secrets_manager.py resolve
```

Clean plaintext merges are re-encrypted and staged automatically. If Git
cannot merge the plaintext cleanly, the manager writes a local-only `.merge`
file below `.git/sops-secret-conflicts/`. Edit that file, remove all conflict
markers, and continue:

```bash
python scripts/secrets/secrets_manager.py resolve --continue
```

To choose one side without merging:

```bash
python scripts/secrets/secrets_manager.py resolve --ours
python scripts/secrets/secrets_manager.py resolve --theirs
```

`.gitattributes` marks `.secrets/` as binary and disables Git's ciphertext
merge attempt, ensuring the manager receives intact base, ours, and theirs
stages.

## Safety behavior

The pre-commit hook:

1. Reads the ordered globs in `secret-patterns`.
2. Mirrors those patterns into the clone-local `.git/info/exclude`.
3. Removes newly added matching plaintext files from the index.
4. Encrypts each matching plaintext file as binary SOPS JSON.
5. Stages only changed files below `.secrets/`.
6. Refuses to commit if a matching plaintext file was already tracked or an
   encrypted secret still has a Git conflict.

If a real secret was ever committed in plaintext, removing the file is not
enough: rotate the credential immediately and separately clean the Git history.
