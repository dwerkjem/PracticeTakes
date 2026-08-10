## 1. Harden the derivation key

- [x] 1.1 Set a passphrase on `~/.ssh/id_ed25519` with `ssh-keygen -p -f ~/.ssh/id_ed25519`, and confirm `ssh-keygen -y -P "" -f ~/.ssh/id_ed25519` now fails
- [ ] 1.2 Confirm the key still authenticates to GitHub with `ssh -T git@github.com`
- [x] 1.3 Make `ssh-to-age` available. `nix profile add` fails on a pre-existing `gcc-wrapper` vs `home-manager-path` conflict over `bin/c++`, unrelated to this change and not worth resolving (removing `gcc-wrapper` risks the C++ toolchain). Instead: `nix build nixpkgs#ssh-to-age --out-link ~/.local/state/nix-ssh-to-age` (registers a GC root) then symlink into `~/.local/bin`, which is already on PATH
- [x] 1.4 Derive the recipient with `ssh-to-age -i ~/.ssh/id_ed25519.pub` and confirm it equals `age18qqk80sc9s3dnv2r4w8s32d9mllmzdrnj7shdx5lrsvwhgkh9edq2we8l3`; stop if it differs. The public-side derivation needs no passphrase, so this works before or after 1.1
- [x] 1.5 Confirm the private-side derivation works against the now-protected key: `ssh-to-age -private-key -stdinpass -i ~/.ssh/id_ed25519` (passphrase on stdin). Without `-stdinpass` it fails with "this private key is passphrase protected"

## 2. Add the new recipient without removing the old

- [x] 2.1 Add the derived recipient to `.sops.yaml` alongside `age14q6ph…`, as a comma-separated `age:` list, with a comment naming the SSH key it derives from
- [x] 2.2 Run `sops updatekeys` on all three files under `.secrets/`
- [x] 2.3 Confirm each file now lists both recipients (`grep -c recipient` or inspect the `sops.age` block)
- [x] 2.4 Confirm the old key still decrypts all three, so this step is reversible

## 3. Verify the new recipient in isolation — the gate

- [x] 3.1 Export the derived identity to a tmpfs file, supplying the passphrase on stdin: `ssh-to-age -private-key -stdinpass -i ~/.ssh/id_ed25519 > /dev/shm/agekey.txt` (`chmod 600` it first)
- [x] 3.2 Decrypt all three files with `SOPS_AGE_KEY_FILE=/dev/shm/agekey.txt` in an environment where `~/.config/sops/age/keys.txt` is unreachable (rename it for the duration), and diff each result against the working-tree plaintext
- [x] 3.3 Restore `~/.config/sops/age/keys.txt` and `shred -u /dev/shm/agekey.txt`
- [x] 3.4 **STOP if any file failed.** Leave the old recipient in place and diagnose before continuing; every later step assumes this gate passed

## 4. Build the offline recovery copy

- [ ] 4.1 Run `bash /tmp/provision-vault.sh`, setting a memorable recovery passphrase and recording it on paper
- [ ] 4.2 Confirm the script reported both unlock paths verified and locked the drive on exit
- [ ] 4.3 Re-run with `--refresh` and confirm it unlocks with the SSH key alone, with no passphrase prompt
- [ ] 4.4 Back up the LUKS header: `sudo cryptsetup luksHeaderBackup /dev/sdc2 --header-backup-file ~/vault-luks-header.img`, stored off the stick
- [ ] 4.5 Confirm the stick holds `bootstrap/id_ed25519`, `bootstrap/sops-age-key.txt`, and both `encrypted/` and `plaintext/` copies of all three secrets

## 5. Retire the old recipient

- [x] 5.1 Remove `age14q6ph…` from `.sops.yaml`
- [x] 5.2 Run `sops updatekeys` on all three files
- [x] 5.3 Repeat the isolation check from 3.2 — decrypt with the derived identity alone and diff against plaintext
- [x] 5.4 Confirm `~/.config/sops/age/keys.txt` no longer decrypts the files, proving the old recipient is genuinely gone

## 6. Untrack the encrypted mirrors

- [x] 6.1 `git rm -r --cached .secrets/`
- [x] 6.2 Add `/.secrets/` to `.gitignore`
- [x] 6.3 Confirm `git ls-files .secrets/` is empty and `git status` shows nothing under `.secrets/`
- [x] 6.4 Confirm the files still exist on disk and still decrypt

## 7. Rotate the exposed credentials

- [x] 7.1 Roll `CLOUDFLARE_API_TOKEN` in the Cloudflare dashboard; update `.env`; confirm the old token is revoked
- [x] 7.2 Regenerate `ADMIN_PASSWORD` with `openssl rand -base64 32`; update `.env`
- [ ] 7.3 Regenerate `SUBMISSION_SIGNING_KEY` (≥32 chars) in `.dev.vars` and as the deployed Worker secret
- [ ] 7.4 Re-encrypt with `secrets_manager.py encrypt` and refresh the stick with `bash /tmp/provision-vault.sh --refresh`
- [ ] 7.5 Redeploy the worker; confirm a feedback submission succeeds end to end
- [x] 7.6 Confirm rate-limit counters reset as expected and no stored submissions were lost (`SELECT COUNT(*) FROM feedback_submissions` before and after)

## 8. Shrink the managed-secret set

- [x] 8.1 Copy the real `database_id` into `src/services/feedback-intake/wrangler.example.jsonc`, then `git mv` it to `wrangler.jsonc`
- [x] 8.2 Reconcile the `EMAIL_QUEUE_RETENTION_DAYS` drift — decide whether the worker uses it, and keep or drop it in the single tracked file
- [x] 8.3 Remove the `src/services/feedback-intake/wrangler.jsonc` line from `tools/secret-patterns`
- [x] 8.4 Delete `.secrets/src/services/feedback-intake/wrangler.jsonc.sops`
- [x] 8.5 Confirm `secrets_manager.py audit` passes and `wrangler deploy --dry-run` still resolves the D1 binding

## 9. Narrow the pre-commit gate

- [x] 9.1 Change `secrets_manager.py pre-commit` so it no longer encrypts and stages `.secrets/` mirrors, retaining the plaintext-rejection guard
- [x] 9.2 Update `tools/scripts/secrets/test_secrets_manager.py` to cover the narrowed behaviour, including that a staged plaintext secret is still refused
- [x] 9.3 Update `.pre-commit-config.yaml` if the hook's name or arguments change
- [x] 9.4 Run `python3 tools/scripts/run_tests.py` and confirm the full suite passes
- [x] 9.5 Run `pre-commit run --all-files` and confirm the gate behaves as intended

## 10. Documentation and close-out

- [x] 10.1 Rewrite `docs/development/operations/SECRETS.md` for the new model: derived recipient, untracked mirrors, the two recovery paths, and the rotation obligation
- [x] 10.2 Add the recovery-stick bootstrap procedure, including which unlock path a machine with nothing uses
- [x] 10.3 `docs/development/agents/issue-tracker.md` — checked, no reference to the secrets workflow, no change needed
- [x] 10.4 `QA_STRATEGY.md` area 12 — checked, already states the gap accurately and this change does not affect it; no edit made rather than adding filler
- [x] 10.5 Delete or retire `tools/scripts/secrets/vault_sync.py` and `test_vault_sync.py`, whose transport role SOPS now fills
- [x] 10.6 Move `/tmp/provision-vault.sh` into `tools/scripts/secrets/` so the recovery procedure is versioned
- [x] 10.7 Confirm no plaintext or ciphertext secret is tracked: `git ls-files | grep -E '\.secrets/|\.env$|\.dev\.vars$'` returns nothing
