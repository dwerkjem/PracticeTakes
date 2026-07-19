from __future__ import annotations

import argparse
import sys
from concurrent.futures import Future, ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import date, datetime
from pathlib import Path
from typing import Any

from .config import (
    FIELD_OPTIONS,
    OWNER,
    PROJECT_DESCRIPTION,
    PROJECT_TITLE,
    REPO,
    ROOT_ISSUE,
    STATUS_OPTIONS,
    VIEW_SPECS,
)
from .discovery import list_all_issues
from .github import GitHubClient, GitHubError, Project
from .models import Issue, PlannedIssue, ProjectItem
from .planning import descendants_of, next_monday, parent_map, plan_issues
from .progress import ProgressBar
from .state import StateLockError, StateStore, stable_hash


def log(message: str) -> None:
    """Print a timestamped progress message immediately."""
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}", flush=True)


def warning(message: str) -> None:
    """Print a visible warning without stopping the setup."""
    print(f"WARNING: {message}", file=sys.stderr, flush=True)


def _positive_jobs(value: str) -> int:
    jobs = int(value)
    if jobs < 1 or jobs > 16:
        raise argparse.ArgumentTypeError("--jobs must be between 1 and 16")
    return jobs


def _short_title(title: str, limit: int = 46) -> str:
    return title if len(title) <= limit else f"{title[: limit - 3]}..."


def _normalize_field_value(value: Any) -> str | None:
    if value is None:
        return None
    if isinstance(value, dict):
        for key in ("name", "title", "value", "date", "text"):
            if key in value:
                return _normalize_field_value(value[key])
        return stable_hash(value)
    if isinstance(value, list):
        return ",".join(
            item for item in (_normalize_field_value(entry) for entry in value) if item
        )
    return str(value)


def _current_field_value(item: ProjectItem, field_name: str) -> str | None:
    wanted = field_name.casefold()
    for key, value in item.field_values.items():
        if key.casefold() == wanted:
            return _normalize_field_value(value)
    return None


def _values_match(current: str | None, desired: str) -> bool:
    if current is None:
        return False
    if desired.count("-") == 2 and len(desired) == 10:
        return current[:10] == desired
    return current == desired


@dataclass
class FieldApplyResult:
    issue_number: int
    failures: int
    fingerprint: str


@dataclass
class FieldAssignmentSummary:
    failed_updates: int = 0
    cached_issues: int = 0
    already_current_issues: int = 0
    api_issues: int = 0


@dataclass
class SetupResult:
    project: Project
    discovered_count: int
    selected_count: int
    missing_items: list[int]
    duplicate_items: list[int]
    failed_subissues: list[str]
    failed_views: list[str]
    field_summary: FieldAssignmentSummary
    jobs: int
    state_path: Path | None


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create or update the Practice Takes GitHub Projects roadmap."
    )
    parser.add_argument(
        "--roadmap-only",
        action="store_true",
        help="Include only issues descending from --root-issue instead of every issue.",
    )
    parser.add_argument(
        "--root-issue",
        type=int,
        default=ROOT_ISSUE,
        help=f"Roadmap root issue used for dynamic discovery (default: {ROOT_ISSUE}).",
    )
    parser.add_argument(
        "--start-date",
        type=date.fromisoformat,
        default=None,
        metavar="YYYY-MM-DD",
        help="Roadmap start date. Defaults to the next Monday.",
    )
    parser.add_argument(
        "--jobs",
        type=_positive_jobs,
        default=4,
        metavar="N",
        help="Concurrent GitHub operations for independent issues (default: 4).",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Ignore the local field cache and rewrite managed project values.",
    )
    parser.add_argument(
        "--no-cache",
        action="store_true",
        help="Do not read or write the local incremental state cache.",
    )
    parser.add_argument(
        "--clear-cache",
        action="store_true",
        help="Delete cached successful field assignments before running.",
    )
    parser.add_argument(
        "--state-file",
        type=Path,
        default=None,
        metavar="PATH",
        help="Override the local JSON state-file location.",
    )
    parser.add_argument(
        "--skip-subissues", action="store_true", help="Do not create native sub-issues."
    )
    parser.add_argument(
        "--reset-statuses",
        action="store_true",
        help="Overwrite existing project Status values with the initial roadmap status.",
    )
    parser.add_argument(
        "--skip-views", action="store_true", help="Do not create project views."
    )
    return parser.parse_args(argv)


def configure_fields(client: GitHubClient, project: Project) -> list[dict]:
    log("Reading existing project fields...")
    fields = client.list_fields(project)
    log(f"Found {len(fields)} existing project fields.")

    log("Ensuring field 1/7: Status")
    fields = client.ensure_single_select_field(
        project, fields, "Status", STATUS_OPTIONS
    )

    custom_fields = list(FIELD_OPTIONS.items())
    for index, (name, options) in enumerate(custom_fields, start=2):
        log(f"Ensuring field {index}/7: {name}")
        fields = client.ensure_single_select_field(project, fields, name, options)

    log("Ensuring field 6/7: Start date")
    fields = client.ensure_date_field(project, fields, "Start date")
    log("Ensuring field 7/7: Target date")
    fields = client.ensure_date_field(project, fields, "Target date")

    log("Ensuring optional Iteration field...")
    fields = client.ensure_iteration_field(project, fields, "Iteration")
    refreshed = client.list_fields(project)
    log(f"Project field configuration complete ({len(refreshed)} fields visible).")
    return refreshed


def _add_one_issue(
    client: GitHubClient,
    project: Project,
    issue: Issue,
) -> tuple[Issue, ProjectItem | None]:
    raw = client.json(
        "project",
        "item-add",
        str(project.number),
        "--owner",
        client.owner,
        "--url",
        issue.url,
        "--format",
        "json",
        check=False,
    )
    if raw and raw.get("id"):
        return issue, ProjectItem(
            item_id=str(raw["id"]),
            issue_number=issue.number,
        )
    return issue, None


def add_issues_with_progress(
    client: GitHubClient,
    project: Project,
    selected: dict[int, Issue],
    existing: dict[int, ProjectItem],
    jobs: int,
) -> dict[int, ProjectItem]:
    missing = [selected[number] for number in sorted(set(selected) - set(existing))]
    if not missing:
        log("All selected issues are already present in the project.")
        return existing

    progress = ProgressBar("Adding issues", len(missing))
    with ThreadPoolExecutor(max_workers=min(jobs, len(missing))) as executor:
        futures = {
            executor.submit(_add_one_issue, client, project, issue): issue
            for issue in missing
        }
        for future in as_completed(futures):
            issue = futures[future]
            try:
                completed_issue, item = future.result()
            except Exception as exc:
                progress.message(
                    f"WARNING: issue #{issue.number} could not be added: {exc}",
                    error=True,
                )
                progress.advance(f"#{issue.number} failed")
                continue

            if item is None:
                progress.message(
                    f"WARNING: issue #{completed_issue.number} was not added; "
                    "GitHub did not return a project item ID.",
                    error=True,
                )
                progress.advance(f"#{completed_issue.number} failed")
            else:
                existing[completed_issue.number] = item
                progress.advance(
                    f"#{completed_issue.number} {_short_title(completed_issue.title)}"
                )

    log("Refreshing project items after additions...")
    return client.list_project_items(project)


def _desired_field_values(
    plan: PlannedIssue,
    item: ProjectItem,
    reset_statuses: bool,
) -> tuple[dict[str, str], bool]:
    existing_status = _current_field_value(item, "Status")
    preserve_status = not reset_statuses and existing_status not in (None, "")
    values: dict[str, str] = {
        "Priority": plan.priority,
        "Estimate": plan.estimate,
    }
    if plan.stage is not None:
        values["Stage"] = plan.stage.name
        values["Target window"] = plan.stage.target_window
    if preserve_status:
        values["Status"] = existing_status or plan.status
    else:
        values["Status"] = plan.status
    if plan.start_date is not None:
        values["Start date"] = plan.start_date.isoformat()
    if plan.target_date is not None:
        values["Target date"] = plan.target_date.isoformat()
    return values, preserve_status


def _field_fingerprint(
    project: Project,
    item: ProjectItem,
    plan: PlannedIssue,
    values: dict[str, str],
    preserve_status: bool,
) -> str:
    return stable_hash(
        {
            "version": 2,
            "project_node_id": project.node_id,
            "item_id": item.item_id,
            "issue_node_id": plan.issue.node_id,
            "values": values,
            "preserve_status": preserve_status,
        }
    )


def _live_values_match(item: ProjectItem, values: dict[str, str]) -> bool:
    return all(
        _values_match(_current_field_value(item, field_name), desired)
        for field_name, desired in values.items()
    )


def _apply_fields_for_issue(
    client: GitHubClient,
    project: Project,
    item: ProjectItem,
    plan: PlannedIssue,
    fields_by_name: dict[str, dict],
    values: dict[str, str],
    preserve_status: bool,
    force: bool,
    fingerprint: str,
) -> FieldApplyResult:
    failures = 0
    for field_name in ("Stage", "Priority", "Estimate", "Target window", "Status"):
        desired = values.get(field_name)
        if desired is None:
            continue
        if field_name == "Status" and preserve_status:
            continue
        current = _current_field_value(item, field_name)
        if not force and _values_match(current, desired):
            continue

        field = fields_by_name.get(field_name)
        field_id = str(field["id"]) if field and field.get("id") else None
        option_id = client.option_id(field, desired)
        if not client.set_single_select(project, item, field_id, option_id):
            failures += 1

    for field_name in ("Start date", "Target date"):
        desired = values.get(field_name)
        if desired is None:
            continue
        current = _current_field_value(item, field_name)
        if not force and _values_match(current, desired):
            continue

        field = fields_by_name.get(field_name)
        field_id = str(field["id"]) if field and field.get("id") else None
        if not client.set_date(project, item, field_id, desired):
            failures += 1

    return FieldApplyResult(
        issue_number=plan.issue.number,
        failures=failures,
        fingerprint=fingerprint,
    )


def assign_fields(
    client: GitHubClient,
    project: Project,
    items: dict[int, ProjectItem],
    fields: list[dict],
    plans: list[PlannedIssue],
    reset_statuses: bool,
    state: StateStore,
    *,
    force: bool,
    jobs: int,
) -> FieldAssignmentSummary:
    fields_by_name = {str(field.get("name")): field for field in fields}
    summary = FieldAssignmentSummary()
    progress = ProgressBar("Assigning fields", len(plans))
    pending: dict[Future[FieldApplyResult], PlannedIssue] = {}

    state.prepare_project(project.node_id)
    state.prune_fields({plan.issue.number for plan in plans})

    with ThreadPoolExecutor(max_workers=min(jobs, max(1, len(plans)))) as executor:
        for plan in plans:
            item = items.get(plan.issue.number)
            if item is None:
                progress.message(
                    f"WARNING: issue #{plan.issue.number} is not in the project; "
                    "skipping field assignment.",
                    error=True,
                )
                progress.advance(f"#{plan.issue.number} skipped", measure=False)
                state.forget_field(plan.issue.number)
                continue

            values, preserve_status = _desired_field_values(
                plan,
                item,
                reset_statuses,
            )
            fingerprint = _field_fingerprint(
                project,
                item,
                plan,
                values,
                preserve_status,
            )

            if not force and state.field_is_current(plan.issue.number, fingerprint):
                summary.cached_issues += 1
                progress.advance(f"#{plan.issue.number} cached", measure=False)
                continue

            if not force and _live_values_match(item, values):
                summary.already_current_issues += 1
                state.mark_field_current(plan.issue.number, fingerprint)
                progress.advance(f"#{plan.issue.number} already current", measure=False)
                continue

            future = executor.submit(
                _apply_fields_for_issue,
                client,
                project,
                item,
                plan,
                fields_by_name,
                values,
                preserve_status,
                force,
                fingerprint,
            )
            pending[future] = plan

        state.save()

        for future in as_completed(pending):
            plan = pending[future]
            summary.api_issues += 1
            try:
                result = future.result()
            except Exception as exc:
                summary.failed_updates += 1
                state.forget_field(plan.issue.number)
                progress.message(
                    f"WARNING: field assignment for issue #{plan.issue.number} "
                    f"raised an error: {exc}",
                    error=True,
                )
                progress.advance(f"#{plan.issue.number} failed")
                continue

            summary.failed_updates += result.failures
            if result.failures == 0:
                state.mark_field_current(result.issue_number, result.fingerprint)
                state.save()
                progress.advance(f"#{result.issue_number} updated")
            else:
                state.forget_field(result.issue_number)
                progress.message(
                    f"WARNING: issue #{result.issue_number} completed with "
                    f"{result.failures} failed field update(s).",
                    error=True,
                )
                progress.advance(
                    f"#{result.issue_number} {result.failures} failure(s)"
                )

    state.save()
    return summary


def _link_one_subissue(
    client: GitHubClient,
    parent: Issue,
    child: Issue,
) -> tuple[int, int, bool]:
    return parent.number, child.number, client.ensure_sub_issue(parent, child)


def ensure_subissues_with_progress(
    client: GitHubClient,
    selected: dict[int, Issue],
    plans: list[PlannedIssue],
    jobs: int,
) -> list[str]:
    subissue_plans = [plan for plan in plans if plan.parent_number is not None]
    failed_subissues: list[str] = []
    progress = ProgressBar("Linking sub-issues", len(subissue_plans))
    pending: dict[Future[tuple[int, int, bool]], tuple[Issue, Issue]] = {}

    with ThreadPoolExecutor(
        max_workers=min(jobs, max(1, len(subissue_plans)))
    ) as executor:
        for plan in subissue_plans:
            parent = selected.get(plan.parent_number) if plan.parent_number else None
            if parent is None:
                failed_subissues.append(
                    f"{plan.parent_number}->{plan.issue.number}"
                )
                progress.message(
                    f"WARNING: parent #{plan.parent_number} was not selected for "
                    f"child #{plan.issue.number}.",
                    error=True,
                )
                progress.advance(
                    f"#{plan.issue.number} missing parent",
                    measure=False,
                )
                continue

            if plan.issue.native_parent_number == parent.number:
                progress.advance(
                    f"#{parent.number} -> #{plan.issue.number} already linked",
                    measure=False,
                )
                continue

            if plan.issue.native_parent_number is not None:
                failed_subissues.append(f"{parent.number}->{plan.issue.number}")
                progress.message(
                    f"WARNING: issue #{plan.issue.number} already has parent "
                    f"#{plan.issue.native_parent_number}; expected #{parent.number}.",
                    error=True,
                )
                progress.advance(
                    f"#{parent.number} -> #{plan.issue.number} conflict",
                    measure=False,
                )
                continue

            future = executor.submit(
                _link_one_subissue,
                client,
                parent,
                plan.issue,
            )
            pending[future] = (parent, plan.issue)

        for future in as_completed(pending):
            parent, child = pending[future]
            relationship = f"#{parent.number} -> #{child.number}"
            try:
                _, _, succeeded = future.result()
            except Exception as exc:
                succeeded = False
                progress.message(
                    f"WARNING: sub-issue link {relationship} raised an error: {exc}",
                    error=True,
                )

            if succeeded:
                progress.advance(relationship)
            else:
                failed_subissues.append(f"{parent.number}->{child.number}")
                progress.message(
                    f"WARNING: could not ensure sub-issue link {relationship}.",
                    error=True,
                )
                progress.advance(f"{relationship} failed")

    return failed_subissues


def setup(args: argparse.Namespace, state: StateStore) -> SetupResult:
    log("Practice Takes roadmap setup starting.")
    log(f"Repository: {OWNER}/{REPO}")
    log(
        "Mode: roadmap descendants only"
        if args.roadmap_only
        else "Mode: all repository issues"
    )
    log(
        f"Concurrency: {args.jobs} worker(s); "
        f"cache: {'disabled' if args.no_cache else state.path}"
    )

    client = GitHubClient(OWNER, REPO)

    log(
        "Step 1/9: checking required commands, GitHub authentication, "
        "and repository access..."
    )
    client.preflight()
    log("Step 1/9 complete: GitHub authentication and repository access verified.")

    log("Step 2/9: discovering every repository issue through paginated GraphQL...")
    all_issues = list_all_issues(client)
    if not all_issues:
        raise GitHubError("The repository has no issues to add to the roadmap.")
    log(f"Step 2/9 complete: discovered {len(all_issues)} issues.")

    log("Step 3/9: resolving parent relationships and selecting roadmap issues...")
    parents = parent_map(all_issues)
    log(f"Detected {len(parents)} parent-child issue relationships.")
    selected = (
        descendants_of(all_issues, parents, args.root_issue)
        if args.roadmap_only
        else all_issues
    )
    if not selected:
        raise GitHubError("No roadmap issues were discovered.")
    log(f"Step 3/9 complete: selected {len(selected)} issues.")

    start = args.start_date or next_monday()
    log(
        "Step 4/9: calculating stages, dependencies, estimates, and dates "
        f"from {start}..."
    )
    plans = plan_issues(selected, start)
    planned_dates = sum(
        1 for plan in plans if plan.start_date is not None and plan.target_date is not None
    )
    log(
        f"Step 4/9 complete: generated {len(plans)} plans; "
        f"{planned_dates} have scheduled date ranges."
    )

    log(f"Step 5/9: finding or creating project {PROJECT_TITLE!r}...")
    project = client.get_or_create_project(PROJECT_TITLE, PROJECT_DESCRIPTION)
    log(f"Step 5/9 complete: project #{project.number} at {project.url}")

    log("Step 6/9: reading existing project items...")
    items = client.list_project_items(project)
    existing_count = len(items)
    missing_before = len(set(selected) - set(items))
    log(
        f"Project currently contains {existing_count} issue items; "
        f"{missing_before} selected issues need to be added."
    )
    items = add_issues_with_progress(
        client,
        project,
        selected,
        items,
        args.jobs,
    )
    added_count = max(0, len(items) - existing_count)
    log(
        f"Step 6/9 complete: project now contains {len(items)} issue items "
        f"({added_count} newly detected after refresh)."
    )

    log("Step 7/9: configuring project fields...")
    fields = configure_fields(client, project)
    log("Assigning field values and roadmap dates to each selected issue...")
    field_summary = assign_fields(
        client,
        project,
        items,
        fields,
        plans,
        args.reset_statuses,
        state,
        force=args.force,
        jobs=args.jobs,
    )
    log(
        "Step 7/9 complete: "
        f"{field_summary.cached_issues} cached, "
        f"{field_summary.already_current_issues} already current, "
        f"{field_summary.api_issues} updated through GitHub, "
        f"{field_summary.failed_updates} failed field update(s)."
    )

    if args.skip_subissues:
        log("Step 8/9: native sub-issue linking skipped by --skip-subissues.")
        failed_subissues: list[str] = []
    else:
        total_links = sum(1 for plan in plans if plan.parent_number is not None)
        log(f"Step 8/9: ensuring {total_links} native sub-issue relationships...")
        failed_subissues = ensure_subissues_with_progress(
            client,
            selected,
            plans,
            args.jobs,
        )
        log(
            f"Step 8/9 complete: {total_links - len(failed_subissues)} links "
            f"succeeded or already existed; {len(failed_subissues)} failed."
        )

    if args.skip_views:
        log("Step 9/9: project view creation skipped by --skip-views.")
        failed_views: list[str] = []
    else:
        log(f"Step 9/9: ensuring {len(VIEW_SPECS)} project views...")
        for name, layout, _ in VIEW_SPECS:
            log(f"View requested: {name} ({layout})")
        failed_views = client.ensure_views(project, VIEW_SPECS)
        log(
            f"Step 9/9 complete: view setup finished with "
            f"{len(failed_views)} failure(s)."
        )

    log("Verification: refreshing project items and checking coverage...")
    refreshed = client.list_project_items(project)
    counts: dict[int, int] = {}
    for number in refreshed:
        counts[number] = counts.get(number, 0) + 1
    missing = sorted(set(selected) - set(refreshed))
    duplicates = sorted(number for number, count in counts.items() if count > 1)
    log(
        f"Verification complete: {len(missing)} missing item(s), "
        f"{len(duplicates)} duplicate item(s)."
    )

    state.save()
    return SetupResult(
        project=project,
        discovered_count=len(all_issues),
        selected_count=len(selected),
        missing_items=missing,
        duplicate_items=duplicates,
        failed_subissues=failed_subissues,
        failed_views=failed_views,
        field_summary=field_summary,
        jobs=args.jobs,
        state_path=state.path if state.enabled else None,
    )


def print_report(result: SetupResult) -> None:
    print("\n================ FINAL REPORT ================", flush=True)
    print(f"Project URL: {result.project.url}", flush=True)
    print(
        f"Repository issues discovered dynamically: {result.discovered_count}",
        flush=True,
    )
    print(f"Roadmap issues selected: {result.selected_count}", flush=True)
    print(f"Concurrent workers: {result.jobs}", flush=True)
    print(f"State cache: {result.state_path or 'disabled'}", flush=True)
    print(
        "Field issues: "
        f"{result.field_summary.cached_issues} cached, "
        f"{result.field_summary.already_current_issues} already current, "
        f"{result.field_summary.api_issues} sent to GitHub",
        flush=True,
    )
    print(f"Missing project items: {result.missing_items or 'none'}", flush=True)
    print(f"Duplicate project items: {result.duplicate_items or 'none'}", flush=True)
    print(
        f"Failed field updates: {result.field_summary.failed_updates}",
        flush=True,
    )
    print(f"Failed sub-issue links: {result.failed_subissues or 'none'}", flush=True)
    print(f"Failed view creations: {result.failed_views or 'none'}", flush=True)
    print(
        "No issue maximum is configured; issue discovery uses GitHub pagination.",
        flush=True,
    )
    print("==============================================", flush=True)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    state = StateStore.for_repo(
        OWNER,
        REPO,
        explicit_path=args.state_file,
        enabled=not args.no_cache,
    )

    try:
        with state.locked():
            if args.clear_cache:
                state.clear()
                log(f"Cleared roadmap state cache at {state.path}")
            state.load()
            result = setup(args, state)
    except KeyboardInterrupt:
        print("\nCancelled by user.", file=sys.stderr, flush=True)
        return 130
    except (GitHubError, StateLockError, ValueError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr, flush=True)
        return 1

    print_report(result)
    return 0
