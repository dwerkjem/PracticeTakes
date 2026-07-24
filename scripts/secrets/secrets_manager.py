#!/usr/bin/env python3
"""Manage Practice Takes secret files as SOPS-encrypted Git mirrors.

Plaintext files matched by ``secret-patterns`` stay local. Their encrypted
counterparts are stored at ``.secrets/<original path>.sops`` and may be
committed safely.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable, Sequence


PATTERN_FILE = "secret-patterns"
SECRET_DIRECTORY = ".secrets"
SOPS_SUFFIX = ".sops"
STATE_FILENAME = "sops-secret-state.json"
CONFLICT_DIRECTORY = "sops-secret-conflicts"
EXCLUDE_START = "# BEGIN Practice Takes SOPS secrets"
EXCLUDE_END = "# END Practice Takes SOPS secrets"
PRUNED_DIRECTORIES = {
    ".git",
    SECRET_DIRECTORY,
    ".cache",
    ".venv",
    "Build",
    "build",
    "coverage",
    "dist",
    "env",
    "node_modules",
    "out",
    "venv",
    "vcpkg_installed",
}


class SecretsError(RuntimeError):
    """A safe, user-facing secrets-manager error."""


def run(
    command: Sequence[str],
    *,
    cwd: Path,
    input_data: bytes | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[bytes]:
    """Run a command without passing secret contents through shell arguments."""
    result = subprocess.run(
        command,
        cwd=cwd,
        input=input_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise SecretsError(message or f"{command[0]} exited with {result.returncode}")
    return result


def repository_root() -> Path:
    """Return the current Git repository root."""
    result = run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=Path.cwd(),
    )
    return Path(result.stdout.decode().strip()).resolve()


def validate_relative_path(value: str) -> str:
    """Validate and normalize a repository-relative POSIX path."""
    normalized = value.replace("\\", "/")
    while normalized.startswith("./"):
        normalized = normalized[2:]
    path = PurePosixPath(normalized)
    if not normalized or path.is_absolute() or ".." in path.parts:
        raise SecretsError(f"Unsafe repository-relative path: {value!r}")
    return path.as_posix()


def read_patterns(root: Path) -> list[tuple[bool, str]]:
    """Read ordered include/exclude rules from ``secret-patterns``."""
    path = root / PATTERN_FILE
    if not path.is_file():
        raise SecretsError(f"Missing {PATTERN_FILE} at the repository root")

    rules: list[tuple[bool, str]] = []
    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        excluded = line.startswith("!")
        pattern = line[1:] if excluded else line
        try:
            pattern = validate_relative_path(pattern)
        except SecretsError as error:
            raise SecretsError(f"{PATTERN_FILE}:{number}: {error}") from error
        if pattern.startswith(f"{SECRET_DIRECTORY}/"):
            raise SecretsError(
                f"{PATTERN_FILE}:{number}: patterns cannot target {SECRET_DIRECTORY}/"
            )
        rules.append((excluded, pattern))

    if not any(not excluded for excluded, _ in rules):
        raise SecretsError(f"{PATTERN_FILE} contains no include patterns")
    return rules


def pattern_matches(relative_path: str, pattern: str) -> bool:
    """Match a POSIX path while treating ``**/`` as zero-or-more directories."""
    relative_path = validate_relative_path(relative_path)
    if fnmatch.fnmatchcase(relative_path, pattern):
        return True
    while pattern.startswith("**/"):
        pattern = pattern[3:]
        if fnmatch.fnmatchcase(relative_path, pattern):
            return True
    return False


def is_secret_path(relative_path: str, rules: Iterable[tuple[bool, str]]) -> bool:
    """Apply ordered pattern rules to a path."""
    selected = False
    for excluded, pattern in rules:
        if pattern_matches(relative_path, pattern):
            selected = not excluded
    return selected


def discover_plaintext(
    root: Path, rules: list[tuple[bool, str]]
) -> dict[str, Path]:
    """Find local plaintext files selected by the configured patterns."""
    discovered: dict[str, Path] = {}
    for directory, directory_names, file_names in os.walk(root, topdown=True):
        directory_names[:] = [
            name
            for name in directory_names
            if name not in PRUNED_DIRECTORIES
            and not (Path(directory) / name).is_symlink()
        ]
        current = Path(directory)
        for name in file_names:
            candidate = current / name
            if candidate.is_symlink():
                continue
            relative = candidate.relative_to(root).as_posix()
            if is_secret_path(relative, rules):
                discovered[relative] = candidate
    return dict(sorted(discovered.items()))


def mirror_relative_path(source_relative: str) -> str:
    """Map a plaintext path to its tracked encrypted mirror."""
    source_relative = validate_relative_path(source_relative)
    return f"{SECRET_DIRECTORY}/{source_relative}{SOPS_SUFFIX}"


def source_relative_path(mirror_relative: str) -> str:
    """Map an encrypted mirror back to its plaintext path."""
    mirror_relative = validate_relative_path(mirror_relative)
    prefix = f"{SECRET_DIRECTORY}/"
    if not mirror_relative.startswith(prefix) or not mirror_relative.endswith(SOPS_SUFFIX):
        raise SecretsError(f"Not a managed encrypted secret: {mirror_relative}")
    return mirror_relative[len(prefix) : -len(SOPS_SUFFIX)]


def discover_mirrors(root: Path) -> dict[str, Path]:
    """Find encrypted mirrors and return them keyed by plaintext path."""
    directory = root / SECRET_DIRECTORY
    if not directory.exists():
        return {}
    mirrors: dict[str, Path] = {}
    for path in directory.rglob(f"*{SOPS_SUFFIX}"):
        if path.is_file() and not path.is_symlink():
            relative = path.relative_to(root).as_posix()
            mirrors[source_relative_path(relative)] = path
    return dict(sorted(mirrors.items()))


def sha256(data: bytes) -> str:
    """Return a stable plaintext content digest for local sync state."""
    return hashlib.sha256(data).hexdigest()


def git_directory(root: Path) -> Path:
    """Resolve the local Git metadata directory, including worktrees."""
    result = run(["git", "rev-parse", "--git-dir"], cwd=root)
    path = Path(result.stdout.decode().strip())
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def state_path(root: Path) -> Path:
    return git_directory(root) / STATE_FILENAME


def load_state(root: Path) -> dict[str, str]:
    """Load local-only last-synchronized plaintext hashes."""
    path = state_path(root)
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as error:
        raise SecretsError(f"Could not read local secret sync state: {error}") from error
    if not isinstance(data, dict) or not all(
        isinstance(key, str) and isinstance(value, str) for key, value in data.items()
    ):
        raise SecretsError("Local secret sync state has an invalid format")
    return data


def atomic_write(path: Path, data: bytes, *, private: bool = False) -> None:
    """Write bytes atomically and optionally restrict the resulting file mode."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if private:
            temporary.chmod(0o600)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def save_state(root: Path, state: dict[str, str]) -> None:
    data = json.dumps(dict(sorted(state.items())), indent=2).encode() + b"\n"
    atomic_write(state_path(root), data, private=True)


def require_sops(root: Path) -> str:
    """Return the SOPS executable and reject an unconfigured template."""
    executable = os.environ.get("SOPS", "sops")
    resolved = shutil.which(executable)
    if resolved is None:
        raise SecretsError(
            "SOPS is not installed. Install it from https://getsops.io/docs/install/"
        )
    config = root / ".sops.yaml"
    if not config.exists() and not os.environ.get("SOPS_AGE_RECIPIENTS"):
        raise SecretsError(
            "No .sops.yaml or SOPS_AGE_RECIPIENTS found. Run "
            "'python scripts/secrets/secrets_manager.py init --age-recipient age1...'."
        )
    if config.exists() and "REPLACE_WITH_" in config.read_text(
        encoding="utf-8", errors="replace"
    ):
        raise SecretsError(".sops.yaml still contains a placeholder recipient")
    return resolved


def encrypt_bytes(root: Path, mirror_relative: str, plaintext: bytes) -> bytes:
    """Encrypt arbitrary bytes into SOPS JSON using the mirror path for rules."""
    sops = require_sops(root)
    return run(
        [
            sops,
            "encrypt",
            "--filename-override",
            mirror_relative,
            "--input-type",
            "binary",
            "--output-type",
            "json",
        ],
        cwd=root,
        input_data=plaintext,
    ).stdout


def decrypt_bytes(root: Path, mirror: Path) -> bytes:
    """Decrypt a binary SOPS JSON mirror."""
    sops = require_sops(root)
    return run(
        [
            sops,
            "decrypt",
            "--input-type",
            "json",
            "--output-type",
            "binary",
            str(mirror),
        ],
        cwd=root,
    ).stdout


def stage(root: Path, relative_paths: Iterable[str]) -> None:
    paths = sorted(set(relative_paths))
    if paths:
        run(["git", "add", "--", *paths], cwd=root)


def write_mirror(root: Path, source_relative: str, plaintext: bytes) -> bool:
    """Write an encrypted mirror unless it already contains the same plaintext."""
    mirror_relative = mirror_relative_path(source_relative)
    mirror = root / mirror_relative
    if mirror.exists() and decrypt_bytes(root, mirror) == plaintext:
        return False
    encrypted = encrypt_bytes(root, mirror_relative, plaintext)
    atomic_write(mirror, encrypted)
    return True


def sync_git_exclude(root: Path, rules: list[tuple[bool, str]]) -> None:
    """Keep configured plaintext patterns ignored in this clone."""
    path = git_directory(root) / "info" / "exclude"
    existing = path.read_text(encoding="utf-8") if path.exists() else ""
    lines = existing.splitlines()
    output: list[str] = []
    inside = False
    for line in lines:
        if line == EXCLUDE_START:
            inside = True
            continue
        if line == EXCLUDE_END:
            inside = False
            continue
        if not inside:
            output.append(line)
    if output and output[-1]:
        output.append("")
    output.append(EXCLUDE_START)
    for excluded, pattern in rules:
        output.append(f"!{pattern}" if excluded else pattern)
    # A broad rule such as **/*.secrets can otherwise hide the encrypted
    # mirror directory itself. The mirrors, unlike their sources, are tracked.
    output.extend([f"!{SECRET_DIRECTORY}/", f"!{SECRET_DIRECTORY}/**"])
    output.append(EXCLUDE_END)
    atomic_write(path, ("\n".join(output) + "\n").encode())


def staged_plaintext_paths(
    root: Path, rules: list[tuple[bool, str]]
) -> list[str]:
    result = run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"],
        cwd=root,
    )
    return [
        path
        for path in result.stdout.decode(errors="surrogateescape").split("\0")
        if path and is_secret_path(path, rules)
    ]


def protect_staged_plaintext(
    root: Path, rules: list[tuple[bool, str]]
) -> list[str]:
    """Unstage newly added plaintext secrets and reject already tracked ones."""
    unstaged: list[str] = []
    tracked: list[str] = []
    for relative in staged_plaintext_paths(root, rules):
        existed = run(
            ["git", "cat-file", "-e", f"HEAD:{relative}"],
            cwd=root,
            check=False,
        ).returncode == 0
        if existed:
            tracked.append(relative)
        else:
            run(["git", "rm", "--cached", "--quiet", "--", relative], cwd=root)
            unstaged.append(relative)
    if tracked:
        paths = "\n  ".join(tracked)
        raise SecretsError(
            "Tracked plaintext secret files require explicit removal from Git history/index:\n"
            f"  {paths}\nRun 'git rm --cached -- <path>' after confirming the files are secrets."
        )
    return unstaged


def encrypt_command(root: Path, *, protect_index: bool = False) -> int:
    """Encrypt all matched plaintext files and stage their mirrors."""
    rules = read_patterns(root)
    sync_git_exclude(root, rules)
    unstaged = protect_staged_plaintext(root, rules) if protect_index else []
    plaintext = discover_plaintext(root, rules)
    state = load_state(root)
    staged: list[str] = []
    changed = 0

    for relative, source in plaintext.items():
        data = source.read_bytes()
        if write_mirror(root, relative, data):
            changed += 1
        staged.append(mirror_relative_path(relative))
        state[relative] = sha256(data)

    stage(root, staged)
    save_state(root, state)
    for relative in unstaged:
        print(f"Protected plaintext and staged its encrypted mirror: {relative}")
    print(
        f"Synchronized {len(plaintext)} local secret(s); "
        f"staged {len(staged)} mirror(s), {changed} changed."
    )
    return 0


def decrypt_command(root: Path, *, force: bool) -> int:
    """Restore plaintext files from encrypted mirrors."""
    rules = read_patterns(root)
    sync_git_exclude(root, rules)
    state = load_state(root)
    restored = 0
    for relative, mirror in discover_mirrors(root).items():
        if not is_secret_path(relative, rules):
            raise SecretsError(
                f"{mirror.relative_to(root)} has no matching rule in {PATTERN_FILE}"
            )
        plaintext = decrypt_bytes(root, mirror)
        target = root / relative
        if target.exists() and target.read_bytes() != plaintext and not force:
            raise SecretsError(
                f"Refusing to overwrite changed local secret {relative}; use --force"
            )
        atomic_write(target, plaintext, private=True)
        state[relative] = sha256(plaintext)
        restored += 1
    save_state(root, state)
    print(f"Restored {restored} plaintext secret(s).")
    return 0


def conflict_output_path(root: Path, source_relative: str) -> Path:
    return git_directory(root) / CONFLICT_DIRECTORY / f"{source_relative}.merge"


def record_sync_conflict(
    root: Path, relative: str, local: bytes, encrypted: bytes
) -> None:
    directory = git_directory(root) / CONFLICT_DIRECTORY / relative
    atomic_write(directory.with_suffix(directory.suffix + ".local"), local, private=True)
    atomic_write(
        directory.with_suffix(directory.suffix + ".encrypted"), encrypted, private=True
    )


def sync_command(root: Path, *, prefer: str | None) -> int:
    """Synchronize in both directions using local last-synced hashes."""
    rules = read_patterns(root)
    sync_git_exclude(root, rules)
    plaintext = discover_plaintext(root, rules)
    mirrors = discover_mirrors(root)
    state = load_state(root)
    staged: list[str] = []
    conflicts: list[str] = []

    for relative in sorted(set(plaintext) | set(mirrors)):
        source = plaintext.get(relative)
        mirror = mirrors.get(relative)
        if mirror is not None and not is_secret_path(relative, rules):
            raise SecretsError(
                f"{mirror.relative_to(root)} has no matching rule in {PATTERN_FILE}"
            )

        local_data = source.read_bytes() if source else None
        encrypted_data = decrypt_bytes(root, mirror) if mirror else None
        base_hash = state.get(relative)
        local_hash = sha256(local_data) if local_data is not None else None
        encrypted_hash = sha256(encrypted_data) if encrypted_data is not None else None

        if local_data is None and encrypted_data is not None:
            atomic_write(root / relative, encrypted_data, private=True)
            state[relative] = encrypted_hash
        elif local_data is not None and encrypted_data is None:
            write_mirror(root, relative, local_data)
            staged.append(mirror_relative_path(relative))
            state[relative] = local_hash
        elif local_hash == encrypted_hash:
            state[relative] = local_hash
        elif prefer == "local":
            write_mirror(root, relative, local_data)
            staged.append(mirror_relative_path(relative))
            state[relative] = local_hash
        elif prefer == "encrypted":
            atomic_write(root / relative, encrypted_data, private=True)
            state[relative] = encrypted_hash
        elif base_hash is not None and encrypted_hash == base_hash:
            write_mirror(root, relative, local_data)
            staged.append(mirror_relative_path(relative))
            state[relative] = local_hash
        elif base_hash is not None and local_hash == base_hash:
            atomic_write(root / relative, encrypted_data, private=True)
            state[relative] = encrypted_hash
        else:
            record_sync_conflict(root, relative, local_data, encrypted_data)
            conflicts.append(relative)

    if conflicts:
        save_state(root, state)
        paths = "\n  ".join(conflicts)
        raise SecretsError(
            "Both local and encrypted versions changed (or no sync baseline exists):\n"
            f"  {paths}\nCompare the local-only copies under "
            f"{git_directory(root) / CONFLICT_DIRECTORY}, then rerun sync with "
            "--prefer-local or --prefer-encrypted."
        )

    stage(root, staged)
    save_state(root, state)
    print(f"Synchronized {len(set(plaintext) | set(mirrors))} secret(s).")
    return 0


def unmerged_mirrors(root: Path) -> list[str]:
    result = run(
        ["git", "diff", "--name-only", "--diff-filter=U", "-z"],
        cwd=root,
    )
    paths = result.stdout.decode(errors="surrogateescape").split("\0")
    return sorted(
        path
        for path in paths
        if path.startswith(f"{SECRET_DIRECTORY}/") and path.endswith(SOPS_SUFFIX)
    )


def git_stage_bytes(root: Path, stage_number: int, relative: str) -> bytes | None:
    result = run(
        ["git", "show", f":{stage_number}:{relative}"],
        cwd=root,
        check=False,
    )
    return result.stdout if result.returncode == 0 else None


def decrypt_temporary_ciphertext(root: Path, ciphertext: bytes) -> bytes:
    descriptor, name = tempfile.mkstemp(suffix=".sops.json")
    os.close(descriptor)
    path = Path(name)
    try:
        path.write_bytes(ciphertext)
        path.chmod(0o600)
        return decrypt_bytes(root, path)
    finally:
        path.unlink(missing_ok=True)


def merge_plaintext(ours: bytes, base: bytes, theirs: bytes) -> tuple[bytes, bool]:
    """Three-way merge plaintext with Git's merge-file implementation."""
    directory = Path(tempfile.mkdtemp(prefix="practice-takes-sops-"))
    try:
        paths = [directory / name for name in ("ours", "base", "theirs")]
        for path, data in zip(paths, (ours, base, theirs), strict=True):
            atomic_write(path, data, private=True)
        result = subprocess.run(
            ["git", "merge-file", "-p", *map(str, paths)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode not in (0, 1):
            raise SecretsError(
                result.stderr.decode(errors="replace").strip()
                or "git merge-file failed"
            )
        return result.stdout, result.returncode == 0
    finally:
        shutil.rmtree(directory)


def finish_resolution(root: Path, mirror_relative: str, plaintext: bytes) -> None:
    source_relative = source_relative_path(mirror_relative)
    atomic_write(root / source_relative, plaintext, private=True)
    encrypted = encrypt_bytes(root, mirror_relative, plaintext)
    atomic_write(root / mirror_relative, encrypted)
    stage(root, [mirror_relative])
    state = load_state(root)
    state[source_relative] = sha256(plaintext)
    save_state(root, state)


def resolve_command(
    root: Path,
    *,
    continue_resolution: bool,
    choice: str | None,
) -> int:
    """Resolve Git conflicts by merging decrypted plaintext."""
    if continue_resolution:
        merge_root = git_directory(root) / CONFLICT_DIRECTORY
        merge_files = sorted(merge_root.rglob("*.merge")) if merge_root.exists() else []
        if not merge_files:
            raise SecretsError("No saved secret conflict resolutions were found")
        for merge_file in merge_files:
            plaintext = merge_file.read_bytes()
            if b"<<<<<<<" in plaintext or b">>>>>>>" in plaintext:
                raise SecretsError(f"Conflict markers remain in {merge_file}")
            source_relative = merge_file.relative_to(merge_root).as_posix()
            source_relative = source_relative[: -len(".merge")]
            finish_resolution(root, mirror_relative_path(source_relative), plaintext)
            merge_file.unlink()
        print(f"Completed {len(merge_files)} secret conflict resolution(s).")
        return 0

    conflicts = unmerged_mirrors(root)
    if not conflicts:
        print("No conflicted encrypted secrets were found.")
        return 0

    manual: list[Path] = []
    for mirror_relative in conflicts:
        stages = {
            number: git_stage_bytes(root, number, mirror_relative)
            for number in (1, 2, 3)
        }
        ours_ciphertext = stages[2]
        theirs_ciphertext = stages[3]
        if choice == "ours":
            if ours_ciphertext is None:
                raise SecretsError(f"No ours version exists for {mirror_relative}")
            finish_resolution(
                root,
                mirror_relative,
                decrypt_temporary_ciphertext(root, ours_ciphertext),
            )
            continue
        if choice == "theirs":
            if theirs_ciphertext is None:
                raise SecretsError(f"No theirs version exists for {mirror_relative}")
            finish_resolution(
                root,
                mirror_relative,
                decrypt_temporary_ciphertext(root, theirs_ciphertext),
            )
            continue
        if ours_ciphertext is None or theirs_ciphertext is None:
            raise SecretsError(
                f"{mirror_relative} is an add/delete conflict; use --ours or --theirs"
            )

        ours = decrypt_temporary_ciphertext(root, ours_ciphertext)
        theirs = decrypt_temporary_ciphertext(root, theirs_ciphertext)
        base = (
            decrypt_temporary_ciphertext(root, stages[1])
            if stages[1] is not None
            else b""
        )
        merged, clean = merge_plaintext(ours, base, theirs)
        if clean:
            finish_resolution(root, mirror_relative, merged)
        else:
            source_relative = source_relative_path(mirror_relative)
            output = conflict_output_path(root, source_relative)
            atomic_write(output, merged, private=True)
            manual.append(output)

    if manual:
        locations = "\n  ".join(str(path) for path in manual)
        raise SecretsError(
            "Manual plaintext conflict resolution is required. Edit these local-only files:\n"
            f"  {locations}\nThen run "
            "'python scripts/secrets/secrets_manager.py resolve --continue'."
        )
    print(f"Resolved {len(conflicts)} encrypted secret conflict(s).")
    return 0


def init_command(root: Path, age_recipient: str) -> int:
    """Create a committed-safe SOPS creation rule for an age public key."""
    if not age_recipient.startswith("age1") or any(
        character.isspace() for character in age_recipient
    ):
        raise SecretsError("Expected an age public recipient beginning with 'age1'")
    config = root / ".sops.yaml"
    content = (
        "creation_rules:\n"
        "  - path_regex: ^\\\\.secrets/.*\\\\.sops$\n"
        f"    age: {age_recipient}\n"
    )
    if config.exists() and config.read_text(encoding="utf-8") != content:
        raise SecretsError("Refusing to overwrite the existing .sops.yaml")
    atomic_write(config, content.encode())
    stage(root, [".sops.yaml", PATTERN_FILE])
    sync_git_exclude(root, read_patterns(root))
    print("Created and staged .sops.yaml and secret-patterns.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    initialize = subparsers.add_parser("init", help="configure an age recipient")
    initialize.add_argument("--age-recipient", required=True)

    subparsers.add_parser(
        "encrypt", aliases=["push"], help="encrypt and stage all local secrets"
    )

    decrypt = subparsers.add_parser(
        "decrypt", aliases=["pull"], help="restore all encrypted secrets"
    )
    decrypt.add_argument("--force", action="store_true")

    sync = subparsers.add_parser("sync", help="safely synchronize both directions")
    preference = sync.add_mutually_exclusive_group()
    preference.add_argument("--prefer-local", action="store_true")
    preference.add_argument("--prefer-encrypted", action="store_true")

    subparsers.add_parser(
        "pre-commit", help="protect plaintext, encrypt mirrors, and stage them"
    )

    resolve = subparsers.add_parser(
        "resolve", help="resolve Git conflicts in decrypted plaintext"
    )
    resolution = resolve.add_mutually_exclusive_group()
    resolution.add_argument("--continue", dest="continue_resolution", action="store_true")
    resolution.add_argument("--ours", action="store_true")
    resolution.add_argument("--theirs", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)
    try:
        root = repository_root()
        if arguments.command == "init":
            return init_command(root, arguments.age_recipient)
        if arguments.command in ("encrypt", "push"):
            return encrypt_command(root)
        if arguments.command in ("decrypt", "pull"):
            return decrypt_command(root, force=arguments.force)
        if arguments.command == "sync":
            preference = (
                "local"
                if arguments.prefer_local
                else "encrypted"
                if arguments.prefer_encrypted
                else None
            )
            return sync_command(root, prefer=preference)
        if arguments.command == "pre-commit":
            if unmerged_mirrors(root):
                raise SecretsError(
                    "Encrypted secrets have unresolved Git conflicts. Run the "
                    "'resolve' command before committing."
                )
            return encrypt_command(root, protect_index=True)
        if arguments.command == "resolve":
            choice = "ours" if arguments.ours else "theirs" if arguments.theirs else None
            return resolve_command(
                root,
                continue_resolution=arguments.continue_resolution,
                choice=choice,
            )
        parser.error(f"Unsupported command: {arguments.command}")
    except SecretsError as error:
        print(f"secrets-manager: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
