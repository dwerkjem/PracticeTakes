from __future__ import annotations

import math
import re
from collections import defaultdict, deque
from datetime import date, timedelta
from typing import Iterable

from .config import (
    CRITICAL_TITLE_PATTERNS,
    ROOT_ISSUE,
    STAGES,
    TRACKER_TITLE_PREFIXES,
)
from .models import Issue, PlannedIssue, Stage

_PARENT_RE = re.compile(r"^Parent\s+(?:tracker|roadmap):\s*#(\d+)\s*$", re.I | re.M)
_DEPENDS_LINE_RE = re.compile(r"^Depends on\s+(.+)$", re.I | re.M)
_ISSUE_REF_RE = re.compile(r"#(\d+)")
_ESTIMATE_RE = re.compile(
    r"Estimate:\s*(?P<low>\d+(?:\.\d+)?)"
    r"(?:\s*[–—-]\s*(?P<high>\d+(?:\.\d+)?))?\s*"
    r"(?P<unit>day|days|week|weeks)",
    re.I,
)


def next_monday(today: date | None = None) -> date:
    today = today or date.today()
    days = (7 - today.weekday()) % 7
    return today + timedelta(days=days or 7)


def parse_parent(body: str) -> int | None:
    match = _PARENT_RE.search(body)
    return int(match.group(1)) if match else None


def parse_dependencies(body: str) -> tuple[int, ...]:
    dependencies: set[int] = set()
    for match in _DEPENDS_LINE_RE.finditer(body):
        dependencies.update(int(value) for value in _ISSUE_REF_RE.findall(match.group(1)))
    return tuple(sorted(dependencies))


def parent_map(issues: dict[int, Issue]) -> dict[int, int]:
    result: dict[int, int] = {}
    for issue in issues.values():
        parent = issue.native_parent_number or parse_parent(issue.body)
        if parent in issues:
            result[issue.number] = parent
    return result


def descendants_of(
    issues: dict[int, Issue], parents: dict[int, int], root: int
) -> dict[int, Issue]:
    if root not in issues:
        return issues
    children: dict[int, list[int]] = defaultdict(list)
    for child, parent in parents.items():
        children[parent].append(child)

    selected: set[int] = {root}
    queue: deque[int] = deque([root])
    while queue:
        current = queue.popleft()
        for child in children.get(current, []):
            if child not in selected:
                selected.add(child)
                queue.append(child)
    return {number: issues[number] for number in sorted(selected)}


def tracker_stage(issue: Issue) -> Stage | None:
    title = issue.title.strip().casefold()
    for stage in STAGES:
        if title.startswith(stage.tracker_prefix.casefold()):
            return stage
    return None


def milestone_stage(issue: Issue) -> Stage | None:
    milestone = (issue.milestone_title or "").casefold()
    for stage in STAGES:
        if stage.tracker_prefix.casefold() in milestone:
            return stage
        if stage.name.casefold() in milestone:
            return stage
    return None


def stage_for_issue(
    issue: Issue,
    issues: dict[int, Issue],
    parents: dict[int, int],
) -> Stage | None:
    direct = tracker_stage(issue)
    if direct is not None:
        return direct
    seen: set[int] = set()
    current = issue.number
    while current in parents and current not in seen:
        seen.add(current)
        current = parents[current]
        ancestor = issues.get(current)
        if ancestor is None:
            break
        found = tracker_stage(ancestor)
        if found is not None:
            return found
    milestone = milestone_stage(issue)
    if milestone is not None:
        return milestone
    if issue.number == ROOT_ISSUE:
        return STAGES[0]
    return None


def estimate_size(issue: Issue) -> str:
    match = _ESTIMATE_RE.search(issue.body)
    if not match:
        return "XL" if issue.title.startswith(TRACKER_TITLE_PREFIXES) else "M"
    value = float(match.group("high") or match.group("low"))
    unit = match.group("unit").casefold()
    days = value * 5 if unit.startswith("week") else value
    if days < 2:
        return "XS"
    if days <= 5:
        return "S"
    if days <= 10:
        return "M"
    if days <= 20:
        return "L"
    return "XL"


def estimate_days(size: str) -> int:
    return {"XS": 2, "S": 5, "M": 10, "L": 20, "XL": 30}[size]


def priority_for(issue: Issue, dependencies: tuple[int, ...]) -> str:
    title = issue.title.casefold()
    if any(pattern in title for pattern in CRITICAL_TITLE_PATTERNS):
        return "Critical"
    if issue.title.startswith(TRACKER_TITLE_PREFIXES):
        return "High"
    if "early delivery" in issue.body.casefold() or "week 1" in issue.body.casefold():
        return "High"
    return "Normal" if dependencies else "High"


def status_for(issue: Issue, parents: dict[int, int]) -> str:
    if issue.state.upper() == "CLOSED":
        return "Done"
    if issue.number == ROOT_ISSUE or tracker_stage(issue) == STAGES[0]:
        return "In progress"
    if issue.title.casefold().startswith("feature:") and "feedback" in issue.title.casefold():
        return "In progress"
    if issue.number in parents and "feedback service" in issue.title.casefold():
        return "Ready"
    return "Backlog"


def stage_ranges(start: date) -> dict[str, tuple[date, date]]:
    ranges: dict[str, tuple[date, date]] = {}
    current = start
    for stage in STAGES:
        end = current + timedelta(weeks=stage.duration_weeks) - timedelta(days=1)
        ranges[stage.name] = (current, end)
        current = end + timedelta(days=1)
    return ranges


def _topological_order(
    issues: Iterable[Issue], dependencies: dict[int, tuple[int, ...]]
) -> list[Issue]:
    by_number = {issue.number: issue for issue in issues}
    indegree = {number: 0 for number in by_number}
    children: dict[int, list[int]] = defaultdict(list)
    for child, deps in dependencies.items():
        if child not in by_number:
            continue
        for dependency in deps:
            if dependency in by_number:
                indegree[child] += 1
                children[dependency].append(child)

    def rank(number: int) -> tuple[int, int]:
        issue = by_number[number]
        body = issue.body.casefold()
        title = issue.title.casefold()
        early = 0 if ("early delivery" in body or "feedback service" in title) else 1
        return (early, number)

    ready = sorted((number for number, degree in indegree.items() if degree == 0), key=rank)
    ordered: list[int] = []
    while ready:
        current = ready.pop(0)
        ordered.append(current)
        for child in children.get(current, []):
            indegree[child] -= 1
            if indegree[child] == 0:
                ready.append(child)
                ready.sort(key=rank)

    ordered.extend(sorted(set(by_number) - set(ordered), key=rank))
    return [by_number[number] for number in ordered]


def plan_issues(
    issues: dict[int, Issue],
    roadmap_start: date,
) -> list[PlannedIssue]:
    parents = parent_map(issues)
    dependencies = {
        issue.number: parse_dependencies(issue.body) for issue in issues.values()
    }
    ranges = stage_ranges(roadmap_start)
    stage_members: dict[str, list[Issue]] = defaultdict(list)
    stage_lookup: dict[int, Stage | None] = {}

    for issue in issues.values():
        stage = stage_for_issue(issue, issues, parents)
        stage_lookup[issue.number] = stage
        if stage is not None:
            stage_members[stage.name].append(issue)

    children_by_parent: dict[int, list[int]] = defaultdict(list)
    for child, parent in parents.items():
        children_by_parent[parent].append(child)

    dates: dict[int, tuple[date | None, date | None]] = {}
    roadmap_end = ranges[STAGES[-1].name][1]
    if ROOT_ISSUE in issues:
        dates[ROOT_ISSUE] = (roadmap_start, roadmap_end)

    for stage in STAGES:
        stage_start, stage_end = ranges[stage.name]
        members = stage_members.get(stage.name, [])
        trackers = [
            issue
            for issue in members
            if issue.number == ROOT_ISSUE
            or tracker_stage(issue) is not None
            or issue.number in children_by_parent
        ]
        workers = [issue for issue in members if issue not in trackers]
        for issue in trackers:
            if issue.number != ROOT_ISSUE:
                dates[issue.number] = (stage_start, stage_end)

        ordered = _topological_order(workers, dependencies)
        span_days = max(1, (stage_end - stage_start).days + 1)
        slot = span_days / max(1, len(ordered))
        for index, issue in enumerate(ordered):
            size = estimate_size(issue)
            candidate = stage_start + timedelta(days=math.floor(index * slot))
            for dependency in dependencies.get(issue.number, ()):
                dependency_dates = dates.get(dependency)
                if dependency_dates and dependency_dates[1] is not None:
                    candidate = max(candidate, dependency_dates[1] + timedelta(days=1))
            candidate = min(candidate, stage_end)
            target = min(candidate + timedelta(days=estimate_days(size) - 1), stage_end)
            dates[issue.number] = (candidate, target)

    planned: list[PlannedIssue] = []
    for issue in sorted(issues.values(), key=lambda item: item.number):
        stage = stage_lookup[issue.number]
        deps = dependencies[issue.number]
        start, target = dates.get(issue.number, (None, None))
        planned.append(
            PlannedIssue(
                issue=issue,
                stage=stage,
                parent_number=parents.get(issue.number),
                dependencies=deps,
                priority=priority_for(issue, deps),
                estimate=estimate_size(issue),
                status=status_for(issue, parents),
                start_date=start,
                target_date=target,
            )
        )
    return planned
