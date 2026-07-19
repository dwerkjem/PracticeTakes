from __future__ import annotations

from .github import GitHubClient
from .models import Issue


def list_all_issues(client: GitHubClient) -> dict[int, Issue]:
    """Return every repository issue using GraphQL cursor pagination."""
    query = """
    query($owner: String!, $repo: String!, $endCursor: String) {
      repository(owner: $owner, name: $repo) {
        issues(
          first: 100
          after: $endCursor
          orderBy: {field: CREATED_AT, direction: ASC}
        ) {
          nodes {
            number
            id
            title
            body
            url
            state
            milestone { title }
            parent { number }
          }
          pageInfo { hasNextPage endCursor }
        }
      }
    }
    """
    raw = client.json(
        "api",
        "graphql",
        "--paginate",
        "--slurp",
        "-f",
        f"query={query}",
        "-F",
        f"owner={client.owner}",
        "-F",
        f"repo={client.repo}",
    )
    pages = raw if isinstance(raw, list) else [raw]
    issues: dict[int, Issue] = {}

    for page in pages:
        nodes = (
            (page or {})
            .get("data", {})
            .get("repository", {})
            .get("issues", {})
            .get("nodes", [])
        )
        for item in nodes:
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
                    int(parent["number"])
                    if parent.get("number") is not None
                    else None
                ),
            )
            issues[issue.number] = issue

    return issues
