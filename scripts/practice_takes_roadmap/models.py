from __future__ import annotations

from dataclasses import dataclass, field
from datetime import date
from typing import Any


@dataclass(frozen=True)
class Issue:
    number: int
    node_id: str
    title: str
    body: str
    url: str
    state: str
    milestone_title: str | None = None
    native_parent_number: int | None = None


@dataclass(frozen=True)
class Stage:
    name: str
    tracker_prefix: str
    target_window: str
    duration_weeks: int


@dataclass
class ProjectItem:
    item_id: str
    issue_number: int
    field_values: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class PlannedIssue:
    issue: Issue
    stage: Stage | None
    parent_number: int | None
    dependencies: tuple[int, ...]
    priority: str
    estimate: str
    status: str
    start_date: date | None
    target_date: date | None
