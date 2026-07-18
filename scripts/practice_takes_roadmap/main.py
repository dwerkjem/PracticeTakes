from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from datetime import date, datetime

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


def log(message: str) -> None:
    """Print a timestamped progress message immediately."""
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{timestamp}] {message}", flush=True)


def warning(message: str) -> None:
    """Print a visible warning without stopping the setup."""
    print(f"WARNING: {message}", file=sys.stderr, flush=True)


@dataclass
class SetupResult:
    project: Project
    discovered_count: int
    selected_count: int
    missing_items: list[int]
    duplicate_items: list[int]
    failed_subissues: list[str]
    failed_views: list[str]
    failed_field_updates: int


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


def add_issues_with_progress(
    client: GitHubClient,
    project: Project,
    selected: dict[int, Issue],
    existing: dict[int, ProjectItem],
) -> dict[int, ProjectItem]:
    missing = [selected[number] for number in sorted(set(selected) - set(existing))]
    if not missing:
        log("All selected issues are already present in the project.")
        return existing

    progress = ProgressBar("Adding issues", len(missing))
    for issue in missing:
        short_title = issue.title
        if len(short_title) > 46:
            short_title = f"{short_title[:43]}..."
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
            existing[issue.number] = ProjectItem(
                item_id=str(raw["id"]), issue_number=issue.number
            )
            detail = f"#{issue.number} {short_title}"
        else:
            progress.message(
                f"WARNING: issue #{issue.number} was not added; "
                "GitHub did not return a project item ID.",
                error=True,
            )
            detail = f"#{issue.number} failed"
        progress.advance(detail)

    progress.finish("project items submitted")
    log("Refreshing project items after additions...")
    return client.list_project_items(project)


def assign_fields(
    client: GitHubClient,
    project: Project,
    items: dict[int, ProjectItem],
    fields: list[dict],
    plans: list[PlannedIssue],
    reset_statuses: bool,
) -> int:
    by_name = {str(field.get("name")): field for field in fields}
    failed = 0
    progress = ProgressBar("Assigning fields", len(plans))

    for plan in plans:
        item = items.get(plan.issue.number)
        if item is None:
            progress.message(
                f"WARNING: issue #{plan.issue.number} is not in the project; "
                "skipping field assignment.",
                error=True,
            )
            progress.advance(f"#{plan.issue.number} skipped")
            continue

        failures_before = failed
        values = {
            "Stage": plan.stage.name if plan.stage else None,
            "Priority": plan.priority,
            "Estimate": plan.estimate,
            "Target window": plan.stage.target_window if plan.stage else None,
            "Status": plan.status,
        }
        for field_name, value in values.items():
            if value is None:
                continue
            if field_name == "Status" and not reset_statuses:
                existing_status = next(
                    (
                        existing
                        for key, existing in item.field_values.items()
                        if key.casefold() == "status" and existing not in (None, "")
                    ),
                    None,
                )
                if existing_status is not None:
                    continue
            field = by_name.get(field_name)
            field_id = str(field["id"]) if field and field.get("id") else None
            option_id = client.option_id(field, value)
            if not client.set_single_select(project, item, field_id, option_id):
                failed += 1
                progress.message(
                    f"WARNING: could not set {field_name!r} on "
                    f"issue #{plan.issue.number}.",
                    error=True,
                )

        for field_name, value in (
            ("Start date", plan.start_date),
            ("Target date", plan.target_date),
        ):
            field = by_name.get(field_name)
            field_id = str(field["id"]) if field and field.get("id") else None
            text = value.isoformat() if value else None
            if text and not client.set_date(project, item, field_id, text):
                failed += 1
                progress.message(
                    f"WARNING: could not set {field_name!r} on "
                    f"issue #{plan.issue.number}.",
                    error=True,
                )

        issue_failures = failed - failures_before
        detail = (
            f"#{plan.issue.number} complete"
            if issue_failures == 0
            else f"#{plan.issue.number} {issue_failures} failure(s)"
        )
        progress.advance(detail)

    progress.finish(f"{failed} failed update(s)")
    return failed


def ensure_subissues_with_progress(
    client: GitHubClient,
    selected: dict[int, Issue],
    plans: list[PlannedIssue],
) -> list[str]:
    subissue_plans = [plan for plan in plans if plan.parent_number is not None]
    failed_subissues: list[str] = []
    progress = ProgressBar("Linking sub-issues", len(subissue_plans))

    for plan in subissue_plans:
        parent = selected.get(plan.parent_number) if plan.parent_number else None
        if parent is None:
            progress.message(
                f"WARNING: parent #{plan.parent_number} was not selected for "
                f"child #{plan.issue.number}.",
                error=True,
            )
            failed_subissues.append(f"{plan.parent_number}->{plan.issue.number}")
            progress.advance(f"#{plan.issue.number} missing parent")
            continue

        relationship = f"#{parent.number} -> #{plan.issue.number}"
        if not client.ensure_sub_issue(parent, plan.issue):
            failed_subissues.append(f"{parent.number}->{plan.issue.number}")
            progress.message(
                f"WARNING: could not ensure sub-issue link {relationship}.",
                error=True,
            )
            progress.advance(f"{relationship} failed")
        else:
            progress.advance(relationship)

    progress.finish(f"{len(failed_subissues)} failed link(s)")
    return failed_subissues


def setup(args: argparse.Namespace) -> SetupResult:
    log("Practice Takes roadmap setup starting.")
    log(f"Repository: {OWNER}/{REPO}")
    log(
        "Mode: roadmap descendants only"
        if args.roadmap_only
        else "Mode: all repository issues"
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
    items = add_issues_with_progress(client, project, selected, items)
    added_count = max(0, len(items) - existing_count)
    log(
        f"Step 6/9 complete: project now contains {len(items)} issue items "
        f"({added_count} newly detected after refresh)."
    )

    log("Step 7/9: configuring project fields...")
    fields = configure_fields(client, project)
    log("Assigning field values and roadmap dates to each selected issue...")
    failed_updates = assign_fields(
        client, project, items, fields, plans, args.reset_statuses
    )
    log(
        f"Step 7/9 complete: field assignment finished with "
        f"{failed_updates} failed update(s)."
    )

    if args.skip_subissues:
        log("Step 8/9: native sub-issue linking skipped by --skip-subissues.")
        failed_subissues: list[str] = []
    else:
        total_links = sum(1 for plan in plans if plan.parent_number is not None)
        log(f"Step 8/9: ensuring {total_links} native sub-issue relationships...")
        failed_subissues = ensure_subissues_with_progress(client, selected, plans)
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

    return SetupResult(
        project=project,
        discovered_count=len(all_issues),
        selected_count=len(selected),
        missing_items=missing,
        duplicate_items=duplicates,
        failed_subissues=failed_subissues,
        failed_views=failed_views,
        failed_field_updates=failed_updates,
    )


def print_report(result: SetupResult) -> None:
    print("\n================ FINAL REPORT ================", flush=True)
    print(f"Project URL: {result.project.url}", flush=True)
    print(
        f"Repository issues discovered dynamically: {result.discovered_count}",
        flush=True,
    )
    print(f"Roadmap issues selected: {result.selected_count}", flush=True)
    print(f"Missing project items: {result.missing_items or 'none'}", flush=True)
    print(f"Duplicate project items: {result.duplicate_items or 'none'}", flush=True)
    print(f"Failed field updates: {result.failed_field_updates}", flush=True)
    print(f"Failed sub-issue links: {result.failed_subissues or 'none'}", flush=True)
    print(f"Failed view creations: {result.failed_views or 'none'}", flush=True)
    print(
        "No issue maximum is configured; issue discovery uses GitHub pagination.",
        flush=True,
    )
    print("==============================================", flush=True)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        result = setup(args)
    except KeyboardInterrupt:
        print("\nCancelled by user.", file=sys.stderr, flush=True)
        return 130
    except (GitHubError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr, flush=True)
        return 1
    print_report(result)
    return 0
