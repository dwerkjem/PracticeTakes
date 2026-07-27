## 1. Add Dependabot configuration

- [x] 1.1 Create `.github/dependabot.yml` with a `github-actions` entry
      (weekly, targeting `.github/workflows/`) and an `npm` entry (weekly,
      `directory: /services/feedback-intake`).

## 2. SHA-pin GitHub Actions references

- [x] 2.1 Replace `actions/checkout@v6` with
      `actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803 # v6`
      in every workflow file that uses it.
- [x] 2.2 Replace `actions/cache@v4` with
      `actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830 # v4`
      in every workflow file that uses it.
- [x] 2.3 Replace `actions/setup-node@v4` with
      `actions/setup-node@49933ea5288caeca8642d1e84afbd3f7d6820020 # v4`
      in every workflow file that uses it.
- [x] 2.4 Replace `actions/upload-artifact@v4` with
      `actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02 # v4`
      in every workflow file that uses it.
- [x] 2.5 Replace `actions/download-artifact@v5` with
      `actions/download-artifact@634f93cb2916e3fdff6788551b99b062d0335ce0 # v5`
      in every workflow file that uses it.

## 3. Verification

- [x] 3.1 Confirm `.github/dependabot.yml` is valid YAML and passes
      GitHub's schema (check via `yamllint` or equivalent locally).
- [x] 3.2 Confirm no workflow file still contains a mutable `@vN`
      Actions reference (`grep -r 'uses: actions/' .github/workflows/`
      should show only SHA-pinned lines).
- [x] 3.3 Confirm the existing build and test suite still passes after
      the workflow file edits (no accidental syntax breaks).
