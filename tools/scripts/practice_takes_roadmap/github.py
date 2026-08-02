from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import dataclass
from typing import Any, Iterable

from .models import Issue, ProjectItem


class GitHubError(RuntimeError):
    pass


@dataclass(frozen=True)
class Project:
    node_id: str
    number: int
    url: str


class GitHubClient:
    def __init__(self, owner: str, repo: str) -> None:
        self.owner = owner
        self.repo = repo
        self.repo_nwo = f"{owner}/{repo}"

    @staticmethod
    def require_commands() -> None:
        missing = [name for name in ("gh", "python3") if shutil.which(name) is None]
        if missing:
            raise GitHubError(f"Missing required command(s): {', '.join(missing)}")

    def run(
        self,
        *args: str,
        check: bool = True,
        input_text: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            ["gh", *args],
            input=input_text,
            text=True,
            capture_output=True,
            check=False,
        )
        if check and result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip() or "unknown gh error"
            raise GitHubError(f"gh {' '.join(args)} failed: {detail}")
        return result

    def json(self, *args: str, check: bool = True) -> Any:
        result = self.run(*args, check=check)
        if result.returncode != 0 or not result.stdout.strip():
            return None
        try:
            return json.loads(result.stdout)
        except json.JSONDecodeError as exc:
            raise GitHubError(f"Invalid JSON from gh {' '.join(args)}: {exc}") from exc

    def preflight(self) -> None:
        self.require_commands()
        self.run("auth", "status")
        self.run("repo", "view", self.repo_nwo)

    def list_issues(self) -> dict[int, Issue]:
        raw = self.json(
            "issue",
            "list",
            "--repo",
            self.repo_nwo,
            "--state",
            "all",
            "--limit",
            "100000",
            "--json",
            "number,id,title,body,url,state,milestone,parent",
        )
        issues: dict[int, Issue] = {}
        for item in raw or []:
            milestone = item.get("milestone") or {}
            parent = item.get("parent") or {}
            issue = Issue(
                number=int(item["number"]),
                node_id=str(item["id"]),
                title=str(item.get("title") or ""),
                body=str(item.get("body") or ""),
                url=str(item["url"]),
                state=str(item.get("state") or "OPEN"),
                milestone_title=milestone.get("title"),
                native_parent_number=(
                    int(parent["number"]) if parent.get("number") is not None else None
                ),
            )
            issues[issue.number] = issue
        return issues

    def get_or_create_project(self, title: str, description: str) -> Project:
        projects_raw = self.json(
            "project",
            "list",
            "--owner",
            self.owner,
            "--limit",
            "1000",
            "--format",
            "json",
        ) or []
        projects = (
            projects_raw.get("projects", [])
            if isinstance(projects_raw, dict)
            else projects_raw
        )
        match = next((project for project in projects if project.get("title") == title), None)
        if match is None:
            match = self.json(
                "project",
                "create",
                "--owner",
                self.owner,
                "--title",
                title,
                "--format",
                "json",
            )
        project = Project(
            node_id=str(match["id"]),
            number=int(match["number"]),
            url=str(match["url"]),
        )
        self.run(
            "project",
            "edit",
            str(project.number),
            "--owner",
            self.owner,
            "--description",
            description,
            check=False,
        )
        self.run(
            "project",
            "link",
            str(project.number),
            "--owner",
            self.owner,
            "--repo",
            self.repo_nwo,
            check=False,
        )
        return project

    def list_project_items(self, project: Project) -> dict[int, ProjectItem]:
        raw = self.json(
            "project",
            "item-list",
            str(project.number),
            "--owner",
            self.owner,
            "--limit",
            "100000",
            "--format",
            "json",
        ) or {}
        result: dict[int, ProjectItem] = {}
        for item in raw.get("items", []):
            content = item.get("content") or {}
            number = content.get("number")
            if number is None:
                continue
            fields = {
                key: value
                for key, value in item.items()
                if key not in {"id", "content"}
            }
            result[int(number)] = ProjectItem(
                item_id=str(item["id"]),
                issue_number=int(number),
                field_values=fields,
            )
        return result

    def add_issues(
        self,
        project: Project,
        issues: Iterable[Issue],
        existing: dict[int, ProjectItem],
    ) -> dict[int, ProjectItem]:
        for issue in issues:
            if issue.number in existing:
                continue
            raw = self.json(
                "project",
                "item-add",
                str(project.number),
                "--owner",
                self.owner,
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
        return self.list_project_items(project)

    def list_fields(self, project: Project) -> list[dict[str, Any]]:
        raw = self.json(
            "project",
            "field-list",
            str(project.number),
            "--owner",
            self.owner,
            "--limit",
            "1000",
            "--format",
            "json",
        )
        if isinstance(raw, dict):
            return list(raw.get("fields", []))
        return list(raw or [])

    def ensure_single_select_field(
        self,
        project: Project,
        fields: list[dict[str, Any]],
        name: str,
        options: tuple[str, ...],
    ) -> list[dict[str, Any]]:
        field = next((item for item in fields if item.get("name") == name), None)
        if field is None:
            self.run(
                "project",
                "field-create",
                str(project.number),
                "--owner",
                self.owner,
                "--name",
                name,
                "--data-type",
                "SINGLE_SELECT",
                "--single-select-options",
                ",".join(options),
            )
            return self.list_fields(project)

        existing = {option.get("name") for option in field.get("options", [])}
        missing = [option for option in options if option not in existing]
        if missing:
            print(
                f"WARNING: field {name!r} is missing options {missing}; "
                "GitHub CLI cannot append options to an existing field."
            )
        return fields

    def ensure_date_field(
        self,
        project: Project,
        fields: list[dict[str, Any]],
        name: str,
    ) -> list[dict[str, Any]]:
        if any(item.get("name") == name for item in fields):
            return fields
        self.run(
            "project",
            "field-create",
            str(project.number),
            "--owner",
            self.owner,
            "--name",
            name,
            "--data-type",
            "DATE",
        )
        return self.list_fields(project)

    def ensure_iteration_field(
        self,
        project: Project,
        fields: list[dict[str, Any]],
        name: str,
    ) -> list[dict[str, Any]]:
        if any(item.get("name") == name for item in fields):
            return fields
        query = """
        mutation($project: ID!, $name: String!) {
          createProjectV2Field(input: {
            projectId: $project,
            name: $name,
            dataType: ITERATION
          }) {
            projectV2Field { ... on ProjectV2IterationField { id name } }
          }
        }
        """
        result = self.run(
            "api",
            "graphql",
            "-f",
            f"query={query}",
            "-F",
            f"project={project.node_id}",
            "-F",
            f"name={name}",
            check=False,
        )
        if result.returncode != 0:
            print(f"WARNING: could not create iteration field {name!r}: {result.stderr.strip()}")
        return self.list_fields(project)

    @staticmethod
    def option_id(field: dict[str, Any] | None, option_name: str) -> str | None:
        if field is None:
            return None
        for option in field.get("options", []):
            if option.get("name") == option_name:
                return str(option["id"])
        return None

    def set_single_select(
        self,
        project: Project,
        item: ProjectItem,
        field_id: str | None,
        option_id: str | None,
    ) -> bool:
        if not field_id or not option_id:
            return False
        result = self.run(
            "project",
            "item-edit",
            "--project-id",
            project.node_id,
            "--id",
            item.item_id,
            "--field-id",
            field_id,
            "--single-select-option-id",
            option_id,
            check=False,
        )
        return result.returncode == 0

    def set_date(
        self,
        project: Project,
        item: ProjectItem,
        field_id: str | None,
        value: str | None,
    ) -> bool:
        if not field_id or not value:
            return False
        result = self.run(
            "project",
            "item-edit",
            "--project-id",
            project.node_id,
            "--id",
            item.item_id,
            "--field-id",
            field_id,
            "--date",
            value,
            check=False,
        )
        return result.returncode == 0

    def ensure_sub_issue(self, parent: Issue, child: Issue) -> bool:
        if child.native_parent_number == parent.number:
            return True
        if child.native_parent_number is not None:
            print(
                f"WARNING: issue #{child.number} already has parent "
                f"#{child.native_parent_number}; not replacing it with #{parent.number}."
            )
            return False

        query = """
        mutation($parent: ID!, $child: ID!) {
          addSubIssue(input: {issueId: $parent, subIssueId: $child}) {
            issue { id }
            subIssue { id }
          }
        }
        """
        result = self.run(
            "api",
            "graphql",
            "-f",
            f"query={query}",
            "-F",
            f"parent={parent.node_id}",
            "-F",
            f"child={child.node_id}",
            check=False,
        )
        return result.returncode == 0

    def get_user_database_id(self) -> int | None:
        raw = self.json("api", f"users/{self.owner}", check=False)
        if not raw or raw.get("id") is None:
            return None
        return int(raw["id"])

    def ensure_views(
        self,
        project: Project,
        view_specs: Iterable[tuple[str, str, str]],
    ) -> list[str]:
        user_id = self.get_user_database_id()
        if user_id is None:
            return [name for name, _, _ in view_specs]

        endpoint = f"users/{user_id}/projectsV2/{project.number}/views"
        existing = self.json("api", endpoint, check=False) or []
        if isinstance(existing, dict):
            existing = existing.get("views") or existing.get("items") or []
        names = {view.get("name") for view in existing}
        failed: list[str] = []
        for name, layout, filter_query in view_specs:
            if name in names:
                continue
            payload = json.dumps(
                {"name": name, "layout": layout, "filter": filter_query}
            )
            result = self.run(
                "api",
                "--method",
                "POST",
                "-H",
                "X-GitHub-Api-Version: 2026-03-10",
                endpoint,
                "--input",
                "-",
                check=False,
                input_text=payload,
            )
            if result.returncode != 0:
                failed.append(name)
        return failed
