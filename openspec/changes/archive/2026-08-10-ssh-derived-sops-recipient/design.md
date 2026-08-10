## Context

The repository encrypts three worker secrets with SOPS to a single age
recipient, `age14q6ph…`. The matching private key exists only at
`~/.config/sops/age/keys.txt` on one machine, so that machine failing takes
every managed secret with it. The encrypted mirrors are committed to a public
GitHub repository and have been since `c048d93`.

`ssh-to-age` converts an ed25519 SSH keypair to an age keypair deterministically
— both are Curve25519, so the mapping is a re-encoding rather than a derivation
with parameters that could drift. The developer's key is ed25519, which is the
only type the tool supports; RSA, ECDSA and FIDO `sk-` keys are not convertible.
The full chain was exercised before this design was written: encrypting to the
derived recipient and decrypting through both `SOPS_AGE_KEY_FILE` and
`SOPS_AGE_KEY_CMD` succeeded on sops 3.12.2.

**A passphrase changes how the derivation is invoked.** `ssh-to-age
-private-key` refuses a protected key outright — `ssh: this private key is
passphrase protected` — so every private-side derivation must pass
`-stdinpass` and feed the passphrase on stdin. This was verified against a
throwaway protected key: `-stdinpass` succeeds, and the recipient it yields
matches the one derived from the corresponding public key, so protecting the key
does not change the identity. The public-side derivation (`ssh-to-age -i
key.pub`) needs no passphrase at all, which is why the recipient in `.sops.yaml`
can be recomputed by anyone at any time.

Two properties of the host constrain what this change can honestly claim. The
root filesystem is unencrypted ext4 and swap is unencrypted, so anything the
machine can read, someone holding the machine can read. And
`~/.ssh/id_ed25519` currently has no passphrase.

Of the three managed secrets, `wrangler.jsonc` differs from its tracked example
in exactly one value, a D1 `database_id` — an account-scoped identifier that
grants nothing without separate credentials, and which Cloudflare's own
documentation commits.

## Goals / Non-Goals

**Goals:**

- Remove the single-machine dependency on `~/.config/sops/age/keys.txt`.
- Stop adding secret material to a public, permanent record.
- Make recipient migration recoverable at every intermediate step.
- Give the developer a way back from a machine holding nothing at all.
- Shrink the managed-secret set to things that are actually secret.

**Non-Goals:**

- Rewriting git history. The repository is public and may already be cloned or
  forked; rewriting offers the appearance of retraction without the substance.
  Rotation is the remedy.
- Encrypting the root filesystem or swap. Both are real weaknesses and both are
  out of scope here; this change is scoped to how secret material is keyed and
  where it lives.
- Per-secret or per-person sharing. The recipient list is all-or-nothing for now.
- Replacing SOPS. It stays; only its recipient and its commit behaviour change.

## Decisions

**Derive the recipient from the SSH key rather than distributing an age key.**
The alternative is copying `keys.txt` to each machine, which is the problem
restated: another secret needing a distribution channel. The SSH key already has
one, is already on every machine the developer works from, and is already backed
by an existing habit. The cost is coupling two roles into one key — compromise
of the SSH key now costs GitHub access *and* every secret — which is what makes
the passphrase requirement non-negotiable rather than advisory.

**Add both recipients, verify, then remove the old one.** SOPS re-encrypts the
data key per recipient, so a file can carry several. The failure mode being
designed against is removing the only working recipient before confirming the
replacement, which is unrecoverable and silent — `sops updatekeys` will happily
produce files nobody can read. Holding both across the change means every
intermediate state is decryptable by the key that already works. The verify step
must decrypt with the new identity *alone*, not merely succeed while the old key
is still available in the environment, or it proves nothing.

**Rotate rather than rewrite.** Ciphertext for all three credentials is public
and permanent. Rotation costs minutes and is verifiable against the running
service; history rewriting is expensive, incomplete against existing clones, and
would leave the same credentials valid. Rotation was confirmed safe by reading
the worker: it performs only `crypto.subtle.sign("HMAC", …)` and
`digest("SHA-256", …)`, with no encryption of stored data anywhere, so no key
here can render a stored row unreadable. Rotating `SUBMISSION_SIGNING_KEY`
orphans the persisted `client_hash` values in `authorization_requests` and
`feedback_submissions`, which resets rate-limit counters and invalidates
in-flight authorization tokens. No submission content is affected.

**Keep the plaintext-rejection hook, drop the staging hook.** Once `.secrets/`
is untracked there is nothing to stage, but the guard that refuses to commit a
plaintext secret is the half that prevents accidents and becomes *more*
important, not less, when the encrypted mirror no longer exists as a safety net.

**Track `wrangler.jsonc` and delete its example.** Managing an identifier as a
secret costs a rotation obligation it can never discharge and leaves two files
to drift — they already have, since the example declares
`EMAIL_QUEUE_RETENTION_DAYS` and the real file does not. One tracked file
removes the drift by construction.

**The offline copy carries the SSH key it is unlocked by.** This looks circular
and is not: the LUKS volume has two keyslots. A machine that already has the SSH
key uses the age-wrapped keyfile and types nothing; a machine that has nothing
uses the memorised passphrase, and the first thing it recovers is the SSH key.
Without the second slot the copy would be unopenable in precisely the situation
it exists for.

## Risks / Trade-offs

**Removing the old recipient before the new one is proven** → unrecoverable loss
of all managed secrets. Mitigated by the ordering requirement in the spec, by
verifying with the new identity in isolation, and by the offline copy holding
plaintext as a last resort independent of any age key.

**The SSH key becomes a single point of compromise for two systems** → mitigated
by a passphrase, which the migration warns about and which the spec makes
normative. Not fully mitigated: on an unencrypted root filesystem a passphrase
protects the file at rest but not a running session.

**`ssh-to-age` is a third-party dependency in the trust path** → mitigated by the
conversion being deterministic and verifiable: the derived recipient can be
recomputed at any time and compared against `.sops.yaml`. It is also only needed
to *derive*; the resulting age identity works with stock SOPS if the tool ever
disappears.

**Every decryption now costs a passphrase prompt** → accepted, and partly the
point. `SOPS_AGE_KEY_CMD` can wrap the `-stdinpass` invocation so the prompt
appears once per shell rather than once per file, but the passphrase must not be
baked into a shell profile or an environment variable, which would restore
exactly the exposure the passphrase exists to remove.

**A fresh clone no longer yields a working configuration** → intended, and the
cost of the change. Mitigated by documenting the two recovery paths in
`SECRETS.md` and by the offline copy.

**Rotation resets rate limiting** → accepted. Counters refill; no data is lost.

**Plaintext on the offline copy** → accepted. It sits inside LUKS, and the
alternative — encrypting it to the same age key the copy exists to recover —
would make the last-resort path depend on the thing it is a resort from.

## Migration Plan

Ordered so that every step is reversible until the last one that isn't.

1. Passphrase the SSH key. Everything downstream assumes it.
2. Derive the recipient and record it. Compare against the value in this design.
3. Add the derived recipient alongside the existing one; `sops updatekeys`.
4. Verify: decrypt every managed file with the derived identity **alone**, in an
   environment where the old key is not reachable. This is the gate.
5. Provision the offline copy and verify both of its unlock paths.
6. Remove the old recipient; `sops updatekeys` again; re-verify.
7. Untrack `.secrets/`; add the ignore entry.
8. Rotate the three credentials; redeploy; confirm the worker serves traffic.
9. Track `wrangler.jsonc`, delete the example, narrow `tools/secret-patterns`.
10. Narrow the pre-commit hook; update `SECRETS.md`.

Rollback: steps 1–5 are additive and revert by removing the new recipient.
After step 6 the old key is no longer sufficient, so the offline copy from step 5
is the rollback path. After step 8 rollback is meaningless — the old credentials
no longer exist to roll back to.

## Open Questions

- Should the derived recipient be recorded in `.sops.yaml` with a comment naming
  the SSH key it came from? It aids the next reader and discloses nothing, since
  the recipient is a public key.
- Does a second machine exist to prove the portability claim before step 6? If
  not, step 5's offline copy carries the whole verification burden alone.
