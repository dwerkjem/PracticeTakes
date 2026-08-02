from __future__ import annotations

import sys
from dataclasses import dataclass, field
from time import monotonic
from typing import TextIO


def _format_duration(seconds: float | None) -> str:
    if seconds is None:
        return "calculating"
    seconds = max(0, round(seconds))
    hours, remainder = divmod(seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    if hours:
        return f"{hours}h {minutes:02d}m"
    if minutes:
        return f"{minutes}m {seconds:02d}s"
    return f"{seconds}s"


@dataclass
class ProgressBar:
    """Dependency-free terminal progress bar with elapsed time and ETA."""

    label: str
    total: int
    width: int = 28
    stream: TextIO = sys.stdout
    _current: int = field(init=False, default=0)
    _started_at: float = field(init=False, default_factory=monotonic)
    _last_update_at: float = field(init=False)
    _seconds_per_item: float | None = field(init=False, default=None)
    _detail: str = field(init=False, default="")
    _is_tty: bool = field(init=False)
    _last_render_length: int = field(init=False, default=0)

    def __post_init__(self) -> None:
        self.total = max(0, self.total)
        self._last_update_at = self._started_at
        self._is_tty = bool(getattr(self.stream, "isatty", lambda: False)())
        self._render(final=self.total == 0)

    def advance(self, detail: str = "", amount: int = 1, *, measure: bool = True) -> None:
        now = monotonic()
        amount = max(1, amount)
        previous = self._current
        self._current = min(self.total, self._current + amount)
        completed = self._current - previous
        if completed and measure:
            sample = (now - self._last_update_at) / completed
            if self._seconds_per_item is None:
                self._seconds_per_item = sample
            else:
                self._seconds_per_item = (self._seconds_per_item * 0.7) + (sample * 0.3)
        self._last_update_at = now
        self._detail = detail
        self._render(final=self._current >= self.total)

    def finish(self, detail: str = "complete") -> None:
        self._current = self.total
        self._detail = detail
        self._render(final=True)

    def message(self, message: str, *, error: bool = False) -> None:
        if self._is_tty and self._last_render_length:
            print("\r" + (" " * self._last_render_length) + "\r", end="", file=self.stream, flush=True)
        target = sys.stderr if error else self.stream
        print(message, file=target, flush=True)
        if self._is_tty and self._current < self.total:
            self._render(final=False)

    def _eta_seconds(self) -> float | None:
        if self._current <= 0 or self._seconds_per_item is None:
            return None
        return self._seconds_per_item * max(0, self.total - self._current)

    def _render(self, *, final: bool) -> None:
        elapsed = monotonic() - self._started_at
        ratio = 1.0 if self.total == 0 else self._current / self.total
        filled = min(self.width, round(self.width * ratio))
        bar = "#" * filled + "-" * (self.width - filled)
        percent = ratio * 100
        eta = 0.0 if final else self._eta_seconds()
        detail = f" | {self._detail}" if self._detail else ""
        line = (
            f"{self.label}: [{bar}] {self._current}/{self.total} "
            f"{percent:6.1f}% | elapsed {_format_duration(elapsed)} "
            f"| ETA {_format_duration(eta)}{detail}"
        )

        if self._is_tty:
            padded = line.ljust(self._last_render_length)
            print(f"\r{padded}", end="\n" if final else "", file=self.stream, flush=True)
            self._last_render_length = len(line)
        else:
            print(line, file=self.stream, flush=True)
