# Secret management

Secrets never enter Git — not in plaintext, and not encrypted either. They live
locally in two forms, and a LUKS recovery stick is what rebuilds them on a
machine that has nothing.

```text
src/services/feedback-intake/.env          plaintext, mode 0600, git-ignored
    -> .secrets/src/services/feedback-intake/.env.sops
                                           age-encrypted, git-ignored
```

`tools/secret-patterns` decides which plaintext files are managed. The mirror
under `.secrets/` preserves the original relative path.

## Why encrypted mirrors are not committed

They used to be. The reasoning was that ciphertext is safe to publish, which is
true right up until the key leaks — and a published ciphertext cannot be
retracted, so its confidentiality depends on one key staying secret forever, in
a public repository, with no expiry. Rotation is a better answer than
encryption for material that does not need to travel through Git at all.

The mirrors still exist locally. They are the at-rest copy and the thing the
recovery stick carries. They simply are not tracked.

## The decryption identity

`.sops.yaml` names one age recipient, derived from your SSH key:

```bash
ssh-to-age -i ~/.ssh/id_ed25519.pub
# age18qqk80sc9s3dnv2r4w8s32d9mllmzdrnj7shdx5lrsvwhgkh9edq2we8l3
```

The conversion is deterministic — both are Curve25519, so it is a re-encoding
rather than a key derivation with parameters that could drift. The same SSH key
always yields the same recipient, which is what removes the need to distribute a
separate age key between machines.

**Your SSH key must have a passphrase.** It now decrypts every secret as well as
authenticating to GitHub, so an unprotected key file hands over both. Check
with:

```bash
ssh-keygen -y -P "" -f ~/.ssh/id_ed25519   # must FAIL
```

Adding a passphrase does not change the derived recipient — only the encryption
of the key file on disk.

### The `-stdinpass` trap

`ssh-to-age` refuses a passphrase-protected key outright:

```text
ssh: this private key is passphrase protected
```

The private-side conversion always needs `-stdinpass`, with the passphrase on
stdin:

```bash
ssh-to-age -private-key -stdinpass -i ~/.ssh/id_ed25519
```

The public-side conversion (`-i key.pub`) needs no passphrase at all, which is
why the recipient in `.sops.yaml` can be recomputed by anyone at any time.

Note that `age` behaves differently: it accepts a protected SSH key and prompts
for the passphrase itself. Only `ssh-to-age` needs the flag.

## Setting up a machine that already has your SSH key

```bash
nix build nixpkgs#ssh-to-age --out-link ~/.local/state/nix-ssh-to-age
ln -sf ~/.local/state/nix-ssh-to-age/bin/ssh-to-age ~/.local/bin/ssh-to-age
pre-commit install
```

`--out-link` registers a GC root so `nix-collect-garbage` cannot break the
symlink. `nix profile add` is an alternative, but fails on some profiles with a
pre-existing `bin/c++` conflict between `gcc-wrapper` and `home-manager-path`.

Point SOPS at the derived identity, without ever writing it to disk:

```bash
export SOPS_AGE_KEY_CMD='ssh-to-age -private-key -stdinpass -i ~/.ssh/id_ed25519'
```

Do not bake the passphrase into a shell profile or an environment variable.
That restores exactly the exposure the passphrase exists to remove.

Then restore the plaintext:

```bash
python3 tools/scripts/secrets/secrets_manager.py decrypt
```

## Recovering on a machine that has nothing

A new machine has no SSH key, so it cannot use the stick's SSH keyslot. It uses
the recovery passphrase, and the first thing it recovers is the SSH key —
everything else depends on that.

1. **Unlock with the LUKS recovery passphrase.** The stick has two keyslots:
   slot 0 is the passphrase, slot 1 is a keyfile age-wrapped to your SSH public
   key. Only slot 0 is usable here.

2. **Restore the SSH key** from `bootstrap/` on the stick:

   ```bash
   install -m 600 /mnt/secrets/bootstrap/id_ed25519     ~/.ssh/id_ed25519
   install -m 644 /mnt/secrets/bootstrap/id_ed25519.pub ~/.ssh/id_ed25519.pub
   ```

3. **Restore or re-derive the age identity.** Either copy
   `bootstrap/sops-age-key.txt` to `~/.config/sops/age/keys.txt` (mode 600), or
   derive it from the SSH key with the `-stdinpass` command above.

4. **Clone the repository and decrypt**, or — if SOPS itself is the problem —
   copy the plaintext straight out of `projects/<name>/plaintext/` on the stick.

The stick carries both forms deliberately: the `.sops` copies need a working age
key, the plaintext copies need nothing. They fail in different ways.

Provision or refresh a stick with:

```bash
tools/scripts/secrets/provision-vault.sh              # first time: erases and registers
tools/scripts/secrets/provision-vault.sh --refresh    # reload current secrets
```

It registers the stick's serial in `~/.config/practice-takes/vault.conf` and
refuses to erase anything else without `--reregister`. It also refuses any
non-removable device — note that an externally attached SSD reports
`removable=0` and is therefore excluded, while a USB hard drive in an enclosure
does not, which is why oversized or populated disks trigger a second
confirmation.

Back up the LUKS header off the stick. A corrupt header loses the volume even
when you hold the key:

```bash
sudo cryptsetup luksHeaderBackup /dev/sdX2 --header-backup-file ~/vault-luks-header.img
```

## Daily commands

```bash
python3 tools/scripts/secrets/secrets_manager.py decrypt   # mirrors  -> plaintext
python3 tools/scripts/secrets/secrets_manager.py encrypt   # plaintext -> mirrors
python3 tools/scripts/secrets/secrets_manager.py audit     # fail if a secret is tracked
```

Run `encrypt` before refreshing the stick. After rotating a credential the
plaintext on disk is the newest copy that exists, so the direction that matters
is plaintext → mirror; `decrypt` at that moment would overwrite the new value
with the stale one. `secrets_manager` refuses to do so without `--force`, and
`provision-vault.sh` runs `encrypt` rather than `decrypt` for the same reason.

### The pre-commit hook

`pre-commit install` is required in **every clone**. Two hooks run:

| Hook | Job |
|---|---|
| `sops-secrets` | unstage plaintext secrets; refuse if one is already tracked |
| `sops-secret-audit` | fail if any tracked file matches `tools/secret-patterns` |

The first no longer encrypts or stages anything — there is nothing to stage now
that mirrors are untracked. What remains is the half that prevents accidents,
and it matters more than before: with no encrypted copy going into the
repository, an unnoticed plaintext secret in the index is the only route a
credential has to the remote.

They complement each other. The hook reads `git diff --cached`, so it only sees
a tracked secret when it is being *changed*; `audit` catches one that is
committed and sitting still.

Neither is a guarantee. `git commit --no-verify` skips both, and they match
paths rather than contents, so a credential pasted into source or documentation
is never detected.

## Sourcing `.env` breaks Wrangler

`src/services/feedback-intake/.env` holds a D1-scoped `CLOUDFLARE_API_TOKEN` for
the dashboard daemon. **Wrangler auto-loads `.env` from its working directory**,
so any wrangler command run from that directory silently authenticates with the
D1 token instead of your OAuth session, and fails:

```text
Authentication error [code: 10000]
```

The error blames your token's permissions and never mentions `.env`. `unset` does
not help — wrangler re-reads the file every invocation. Two fixes:

```bash
npx wrangler <command> --env-file /dev/null          # from the service directory
npx wrangler <command> -c src/services/feedback-intake/wrangler.jsonc   # from the repo root
```

`tools/scripts/feedback/migrate-feedback-database.sh --wrangler-login` exists for
this reason; it blanks `CLOUDFLARE_API_TOKEN` before invoking wrangler.

The exception is `wrangler d1 execute`, which genuinely wants that token. Source
`.env` in a subshell so it does not leak into your session.

## Rotation

Rotate when an identity is lost or exposed, when a collaborator leaves, when a
recovery stick goes missing, and on a schedule for the signing key.

| Credential | How | Cost |
|---|---|---|
| `CLOUDFLARE_API_TOKEN` | dashboard → API Tokens → Roll | none |
| `ADMIN_PASSWORD` | `openssl rand -base64 32` | none |
| `SUBMISSION_SIGNING_KEY` | `openssl rand -hex 32 \| npx wrangler secret put SUBMISSION_SIGNING_KEY --env-file /dev/null` | rate-limit counters reset |

Rotating the signing key orphans the stored `client_hash` values in
`authorization_requests` and `feedback_submissions`, because those are HMACs
under the old key. Counters reset and in-flight authorizations expire. **No
submission content is lost** — nothing in D1 is encrypted at rest, so no key
here can render a stored row unreadable.

Put the same value in `.dev.vars`; `wrangler secret put` only updates
production.

Afterwards:

```bash
python3 tools/scripts/secrets/secrets_manager.py encrypt
tools/scripts/secrets/provision-vault.sh --refresh
tools/scripts/feedback/update-dashboard-daemon.sh
( cd src/services/feedback-intake && npx wrangler deploy --env-file /dev/null )
```

`ACCESS_HOSTNAME`, `FEEDBACK_NOTIFICATION_FROM`, `CLOUDFLARE_ACCOUNT_ID` and
`D1_DATABASE_ID` are **not** secrets. They identify resources but grant no
access without a credential. Treating an identifier as a secret buys a rotation
obligation it can never discharge, and forces a template file that will drift
from the real one.

### Changing the recipient

Order matters, and getting it wrong is unrecoverable — `sops updatekeys` will
happily encrypt to a recipient you cannot use, with no error.

1. Add the new recipient **alongside** the old one; `sops updatekeys` every mirror.
2. Verify the new identity decrypts every mirror **alone**, with the old key
   moved out of reach. Verifying while the old key is still readable proves
   nothing.
3. Only then remove the old recipient and `sops updatekeys` again.
4. Confirm the old key can no longer decrypt.

## What this does and does not protect

**Does:** keeps every secret out of Git. Keeps the at-rest mirror unreadable
without your SSH key. Survives losing this machine, via the stick. Survives
losing the stick, because every credential on it is rotatable.

**Does not:** protect the plaintext. `.env` and `.dev.vars` must exist in the
clear for the worker and dashboard to run. They are mode 0600 and git-ignored,
which stops other *users* — it stops nothing that has the disk.

On a machine with an unencrypted root filesystem, anyone holding the drive reads
those files. Unencrypted swap can also page plaintext from memory onto disk,
which is why staging secrets in `tmpfs` is not the protection it appears to be.
The measures that actually close this are, in order of effort: encrypt swap,
then encrypt the root filesystem. Until then the passphrase on your SSH key is
the strongest control in this document, because it is what a stolen laptop does
not come with.
