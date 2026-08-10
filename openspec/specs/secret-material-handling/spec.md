# secret-material-handling Specification

## Purpose
Where secret material may and may not live, who can decrypt it, and what has to
be true before any of that changes.

Two failure modes shape these requirements. The first is publication: ciphertext
committed to a public repository cannot be retracted, so its confidentiality
depends on one key never leaking, indefinitely — which is why encryption is not
accepted as grounds for committing a secret, and why exposure obliges rotation
rather than reassurance. The second is lockout: `sops updatekeys` will happily
re-encrypt to a recipient nobody holds, reporting success, so recipient changes
are ordered such that every intermediate state is still readable by a key that
already works.

Everything else follows from those: a decryption identity derived from a key the
developer already carries rather than one file on one machine, an offline copy
that can rebuild access from nothing, and a standing distinction between a
credential and an identifier that merely names a resource.

## Requirements
### Requirement: Secret material never enters version control
The repository SHALL NOT track secret material in any form. This covers
plaintext files matching `tools/secret-patterns` and the SOPS-encrypted mirrors
under `.secrets/`. Encryption SHALL NOT be treated as sufficient grounds for
committing a secret, because a published ciphertext cannot be retracted and its
confidentiality then depends indefinitely on a single key never leaking.

#### Scenario: A plaintext secret is staged
- **WHEN** a contributor stages a file matching a pattern in `tools/secret-patterns`
- **THEN** the pre-commit gate refuses the commit and names the offending path

#### Scenario: Encrypted mirrors are present but untracked
- **WHEN** `.secrets/` holds SOPS mirrors on a working machine
- **THEN** `git status` reports no untracked or modified files under `.secrets/`,
  and `git ls-files .secrets/` returns nothing

### Requirement: Decryption recipients derive from a portable developer key
`.sops.yaml` SHALL list recipients that are derivable from a key the developer
already carries between machines, rather than a standalone key that exists in
one location. The derivation SHALL be deterministic, so that the same input key
always yields the same recipient and no separate key file has to be
synchronised.

#### Scenario: A second machine decrypts without copying an age key
- **WHEN** a developer has their SSH private key on a machine that has never held
  `~/.config/sops/age/keys.txt`
- **THEN** deriving the age identity from that SSH key decrypts every file under
  `.secrets/`

### Requirement: Recipient migration preserves access at every step
Changing the recipient set SHALL be ordered so that no step can leave managed
files unreadable. A newly added recipient SHALL be proven to decrypt every
managed file before any existing recipient is removed. If verification fails,
the existing recipient SHALL be retained and the migration SHALL halt rather
than continue.

#### Scenario: Both recipients hold before either is dropped
- **WHEN** a new recipient is added to `.sops.yaml`
- **THEN** every file under `.secrets/` is re-encrypted to the old and new
  recipients together, and the old recipient remains listed

#### Scenario: Verification of the new recipient fails
- **WHEN** decryption using only the new recipient fails for any managed file
- **THEN** the old recipient is not removed and the migration stops with the
  failing path reported

#### Scenario: The old recipient is retired
- **WHEN** decryption using only the new recipient has succeeded for every
  managed file
- **THEN** the old recipient may be removed and the files re-encrypted to the
  new recipient alone

### Requirement: The derivation key is passphrase-protected
Where the decryption identity is derived from an SSH key, that SSH key SHALL
require a passphrase. An unprotected key file confers the derived identity on
anyone who can read it, which on a machine without full-disk encryption reduces
the protection of every managed secret to the physical security of the machine.

#### Scenario: Migration is attempted with an unprotected key
- **WHEN** the SSH key that derives the recipient has no passphrase
- **THEN** the operator is warned that every managed secret is readable by anyone
  holding the key file, and is told how to set a passphrase before proceeding

### Requirement: Published ciphertext obliges rotation
Any credential whose ciphertext has been published SHALL be rotated, regardless
of the strength of the encryption, because publication is irreversible and the
guarantee is only as durable as the key. Rotation SHALL be verified against the
running service rather than assumed.

#### Scenario: Secrets are untracked after having been committed
- **WHEN** `.secrets/` is untracked from a repository whose history already
  contains the mirrors
- **THEN** every credential those mirrors held is rotated, and the worker is
  redeployed and confirmed working with the new values

#### Scenario: A rotation would destroy data
- **WHEN** a value cannot be rotated without losing stored data
- **THEN** it is recorded as non-rotatable and given an offline copy, rather than
  being rotated

### Requirement: Access is recoverable from a machine holding nothing
There SHALL be an offline copy sufficient to reconstruct decryption access on a
machine that holds none of the material. Because that copy also holds the key
that unlocks it, it SHALL provide a second unlock path that depends on knowledge
rather than on possession of the key it carries.

#### Scenario: Bootstrapping a machine with no SSH key
- **WHEN** a developer has only the offline copy and a machine with no SSH key,
  no age key, and no clone
- **THEN** they unlock the copy with a memorised passphrase, restore the SSH key
  from it, and decrypt every managed secret

#### Scenario: The offline copy is lost
- **WHEN** the offline copy is lost or destroyed
- **THEN** every credential it held is rotated, and the SSH key it carried is
  replaced and its derived recipient re-encrypted

### Requirement: Resource identifiers are not managed as secrets
A value that names a resource but confers no access to it SHALL NOT be managed
as a secret. Treating identifiers as secrets inflates the set of material
requiring protection and rotation, and makes configuration files unshareable for
no security benefit.

#### Scenario: A configuration file holds only identifiers
- **WHEN** a configuration file's sole non-public value is a resource identifier
  that grants no access without separate credentials
- **THEN** that file is tracked in version control and removed from
  `tools/secret-patterns`, with no parallel example file kept alongside it

