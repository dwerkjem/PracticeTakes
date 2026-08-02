from __future__ import annotations

import json
import os
import tempfile
from contextlib import contextmanager
from datetime import datetime, timezone
from hashlib import sha256
from pathlib import Path
from typing import Any, Iterator

_STATE_SCHEMA = 1


class StateLockError(RuntimeError):
    """Raised when another roadmap setup process already owns the lock."""


def stable_hash(value: Any) -> str:
    """Return a deterministic SHA-256 hash for JSON-compatible state."""
    encoded = json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
        default=str,
    ).encode("utf-8")
    return sha256(encoded).hexdigest()


def _cache_root() -> Path:
    override = os.environ.get("PRACTICE_TAKES_ROADMAP_CACHE_DIR")
    if override:
        return Path(override).expanduser()

    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            return Path(local_app_data)
        return Path.home() / "AppData" / "Local"

    xdg_cache = os.environ.get("XDG_CACHE_HOME")
    return Path(xdg_cache).expanduser() if xdg_cache else Path.home() / ".cache"


def _pid_is_running(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


class StateStore:
    """Atomic local state cache and single-process lock for roadmap setup."""

    def __init__(self, path: Path, *, enabled: bool = True) -> None:
        self.path = path
        self.lock_path = path.with_suffix(path.suffix + ".lock")
        self.enabled = enabled
        self.data: dict[str, Any] = self._empty_state()
        self._dirty = False

    @classmethod
    def for_repo(
        cls,
        owner: str,
        repo: str,
        *,
        explicit_path: Path | None = None,
        enabled: bool = True,
    ) -> "StateStore":
        path = (
            explicit_path.expanduser()
            if explicit_path is not None
            else _cache_root()
            / "practice_takes_roadmap"
            / owner
            / repo
            / "state.json"
        )
        return cls(path, enabled=enabled)

    @staticmethod
    def _empty_state() -> dict[str, Any]:
        return {
            "schema": _STATE_SCHEMA,
            "project_node_id": None,
            "field_assignments": {},
            "updated_at": None,
        }

    def load(self) -> None:
        if not self.enabled or not self.path.exists():
            self.data = self._empty_state()
            return

        try:
            loaded = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            self.data = self._empty_state()
            return

        if not isinstance(loaded, dict) or loaded.get("schema") != _STATE_SCHEMA:
            self.data = self._empty_state()
            return

        assignments = loaded.get("field_assignments")
        if not isinstance(assignments, dict):
            loaded["field_assignments"] = {}
        self.data = loaded

    def clear(self) -> None:
        self.data = self._empty_state()
        self._dirty = False
        if self.path.exists():
            self.path.unlink()

    def prepare_project(self, project_node_id: str) -> None:
        if self.data.get("project_node_id") == project_node_id:
            return
        self.data = self._empty_state()
        self.data["project_node_id"] = project_node_id
        self._dirty = True

    def field_is_current(self, issue_number: int, fingerprint: str) -> bool:
        if not self.enabled:
            return False
        entry = self.data.get("field_assignments", {}).get(str(issue_number))
        return isinstance(entry, dict) and entry.get("fingerprint") == fingerprint

    def mark_field_current(self, issue_number: int, fingerprint: str) -> None:
        if not self.enabled:
            return
        assignments = self.data.setdefault("field_assignments", {})
        assignments[str(issue_number)] = {
            "fingerprint": fingerprint,
            "updated_at": datetime.now(timezone.utc).isoformat(),
        }
        self._dirty = True

    def forget_field(self, issue_number: int) -> None:
        assignments = self.data.get("field_assignments", {})
        if str(issue_number) in assignments:
            del assignments[str(issue_number)]
            self._dirty = True

    def prune_fields(self, selected_numbers: set[int]) -> None:
        assignments = self.data.get("field_assignments", {})
        stale = [key for key in assignments if int(key) not in selected_numbers]
        for key in stale:
            del assignments[key]
            self._dirty = True

    def save(self) -> None:
        if not self.enabled or not self._dirty:
            return

        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.data["schema"] = _STATE_SCHEMA
        self.data["updated_at"] = datetime.now(timezone.utc).isoformat()

        fd, temporary_name = tempfile.mkstemp(
            prefix=f".{self.path.name}.",
            suffix=".tmp",
            dir=self.path.parent,
            text=True,
        )
        temporary_path = Path(temporary_name)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                json.dump(self.data, handle, indent=2, sort_keys=True)
                handle.write("\n")
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary_path, self.path)
        finally:
            if temporary_path.exists():
                temporary_path.unlink()
        self._dirty = False

    @contextmanager
    def locked(self) -> Iterator[None]:
        """Prevent two local setup runs from modifying the same project at once."""
        self.lock_path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "pid": os.getpid(),
            "created_at": datetime.now(timezone.utc).isoformat(),
        }

        for attempt in range(2):
            try:
                descriptor = os.open(
                    self.lock_path,
                    os.O_CREAT | os.O_EXCL | os.O_WRONLY,
                    0o600,
                )
            except FileExistsError:
                existing_pid = 0
                try:
                    existing = json.loads(self.lock_path.read_text(encoding="utf-8"))
                    existing_pid = int(existing.get("pid") or 0)
                except (OSError, ValueError, json.JSONDecodeError):
                    pass

                if attempt == 0 and not _pid_is_running(existing_pid):
                    try:
                        self.lock_path.unlink()
                    except FileNotFoundError:
                        pass
                    continue

                raise StateLockError(
                    "Another Practice Takes roadmap setup appears to be running "
                    f"(lock: {self.lock_path}, pid: {existing_pid or 'unknown'})."
                )
            else:
                with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
                    json.dump(payload, handle)
                    handle.write("\n")
                break

        try:
            yield
        finally:
            try:
                self.lock_path.unlink()
            except FileNotFoundError:
                pass
