## Why

The private half of the repository's only SOPS recipient exists in exactly one
place — `~/.config/sops/age/keys.txt` on a single machine — so losing that
machine makes every `.secrets/*.sops` file permanently unreadable. Meanwhile the
encrypted secrets themselves are committed to a **public** GitHub repository and
have been since `c048d93`, which means their confidentiality rests entirely on
that one key never leaking, forever, with no way to retract what is already
published.

Deriving the recipient from the SSH key the developer already carries removes
the single-machine dependency, and untracking `.secrets/` stops adding to a
public record that cannot be undone.

## What Changes

- **Recipient migration.** `.sops.yaml` moves from the standalone recipient
  `age14q6ph…` to `age18qqk8…`, derived deterministically from
  `~/.ssh/id_ed25519` by `ssh-to-age`. The two are added together and the old
  one is removed only after the new one is proven, because the reverse order is
  unrecoverable.
- **Credential rotation.** The Cloudflare API token, the admin password, and the
  submission signing key are rotated, because their ciphertext is permanently
  public. All three rotate without data loss; rotating the signing key resets
  rate-limit counters and invalidates in-flight authorization tokens, and loses
  no stored feedback.
- **BREAKING — `.secrets/` is untracked.** `git rm -r --cached .secrets/` plus a
  `.gitignore` entry. Every clone stops carrying secrets, so a fresh checkout no
  longer yields a working worker configuration without the recovery stick or an
  existing machine. Git history is deliberately **not** rewritten: the repository
  is public and may already be cloned or forked, so rewriting would provide the
  appearance of retraction without the substance. Rotation is the real remedy.
- **`wrangler.jsonc` leaves the secret set.** Its only non-public value is a D1
  `database_id`, which is an account-scoped identifier, not a credential —
  Cloudflare's own documentation commits these. The real file replaces
  `wrangler.example.jsonc` and is tracked, removing one managed secret entirely.
- **Pre-commit gate narrows.** `secrets_manager.py pre-commit` stops encrypting
  and staging `.secrets/` mirrors, because there is no longer anything to stage.
  The plaintext-rejection guard it also performs is retained — that is the half
  that prevents accidents.
- **New bootstrap path.** A LUKS2 USB stick becomes the cold-storage and
  bootstrap route: it carries the SSH private key, the age key, and copies of the
  secrets in both encrypted and plaintext form. A machine with the SSH key
  unlocks it with that key; a machine without one uses the LUKS recovery
  passphrase, restores the SSH key, and proceeds from there.
- **SSH key hardening becomes load-bearing.** Because the age key is derived from
  it, `~/.ssh/id_ed25519` must carry a passphrase. It currently does not, and
  without one the migration weakens confidentiality rather than improving it:
  the key that opens every secret would be a plaintext file on an unencrypted
  root filesystem.

## Capabilities

### New Capabilities

- `secret-material-handling`: where secret material may and may not live, which
  recipients can decrypt it, the ordering constraints that make recipient
  migration recoverable, and the rotation obligation that follows exposure.

### Modified Capabilities

<!-- None. `python-script-test-gate` requires only that
     tools/scripts/secrets/test_secrets_manager.py executes, which remains true;
     secrets_manager.py is narrowed, not removed. -->

## Impact

- `.sops.yaml` — recipient list.
- `.secrets/` — re-encrypted, then untracked; `.gitignore` gains an entry.
- `tools/secret-patterns` — `src/services/feedback-intake/wrangler.jsonc` removed.
- `tools/scripts/secrets/secrets_manager.py` — `pre-commit` no longer stages
  mirrors; `encrypt`/`decrypt` are unchanged.
- `.pre-commit-config.yaml` — the SOPS staging hook.
- `src/services/feedback-intake/wrangler.jsonc` — tracked;
  `wrangler.example.jsonc` deleted. The example currently declares
  `EMAIL_QUEUE_RETENTION_DAYS`, which the real file does not; that drift is
  resolved by having one file.
- `docs/development/operations/SECRETS.md` — rewritten for the new model.
- Cloudflare — token rolled, admin password reset, `SUBMISSION_SIGNING_KEY`
  replaced and redeployed.
- **Not affected:** the C++ application, the audio path, and CI, which reads
  GitHub Secrets rather than `.secrets/`.
