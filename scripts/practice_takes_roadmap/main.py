from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from datetime import date

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
from .models import PlannedIssue, ProjectItem
from .planning import descendants_of, next_monday, parent_map, plan_issues


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
    fields = client.list_fields(project)
    fields = client.ensure_single_select_field(
        project, fields, "Status", STATUS_OPTIONS
    )
    for name, options in FIELD_OPTIONS.items():
        fields = client.ensure_single_select_field(project, fields, name, options)
    fields = client.ensure_date_field(project, fields, "Start date")
    fields = client.ensure_date_field(project, fields, "Target date")
    fields = client.ensure_iteration_field(project, fields, "Iteration")
    return client.list_fields(project)


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
    for plan in plans:
        item = items.get(plan.issue.number)
        if item is None:
            continue
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

        for field_name, value in (
            ("Start date", plan.start_date),
            ("Target date", plan.target_date),
        ):
            field = by_name.get(field_name)
            field_id = str(field["id"]) if field and field.get("id") else None
            text = value.isoformat() if value else None
            if text and not client.set_date(project, item, field_id, text):
                failed += 1
    return failed


def setup(args: argparse.Namespace) -> SetupResult:
    client = GitHubClient(OWNER, REPO)
    client.preflight()

    all_issues = list_all_issues(client)
    if not all_issues:
        raise GitHubError("The repository has no issues to add to the roadmap.")
    parents = parent_map(all_issues)
    selected = (
        descendants_of(all_issues, parents, args.root_issue)
        if args.roadmap_only
        else all_issues
    )
    if not selected:
        raise GitHubError("No roadmap issues were discovered.")

    start = args.start_date or next_monday()
    plans = plan_issues(selected, start)
    project = client.get_or_create_project(PROJECT_TITLE, PROJECT_DESCRIPTION)
    items = client.list_project_items(project)
    items = client.add_issues(project, selected.values(), items)
    fields = configure_fields(client, project)
    failed_updates = assign_fields(
        client, project, items, fields, plans, args.reset_statuses
    )

    failed_subissues: list[str] = []
    if not args.skip_subissues:
        for plan in plans:
            if plan.parent_number is None:
                continue
            parent = selected.get(plan.parent_number)
            if parent and not client.ensure_sub_issue(parent, plan.issue):
                failed_subissues.append(f"{parent.number}->{plan.issue.number}")

    failed_views = [] if args.skip_views else client.ensure_views(project, VIEW_SPECS)

    refreshed = client.list_project_items(project)
    counts: dict[int, int] = {}
    for number in refreshed:
        counts[number] = counts.get(number, 0) + 1
    missing = sorted(set(selected) - set(refreshed))
    duplicates = sorted(number for number, count in counts.items() if count > 1)

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
    print("\n================ FINAL REPORT ================")
    print(f"Project URL: {result.project.url}")
    print(f"Repository issues discovered dynamically: {result.discovered_count}")
    print(f"Roadmap issues selected: {result.selected_count}")
    print(f"Missing project items: {result.missing_items or 'none'}")
    print(f"Duplicate project items: {result.duplicate_items or 'none'}")
    print(f"Failed field updates: {result.failed_field_updates}")
    print(f"Failed sub-issue links: {result.failed_subissues or 'none'}")
    print(f"Failed view creations: {result.failed_views or 'none'}")
    print("No issue maximum is configured; issue discovery uses GitHub pagination.")
    print("==============================================")


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        result = setup(args)
    except (GitHubError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print_report(result)
    return 0
