#!/usr/bin/env node
/**
 * Gate the knowledge-graph site deployment.
 *
 * Runs before any build work, so a bad graph costs seconds rather than a
 * failed publish. Four checks, in order of how cheaply they fail:
 *
 *   1. shape       — the graph parses and carries what the dashboard needs
 *   2. excluded    — no node references a path .understandignore excludes
 *   3. provenance  — the graph names a commit that exists in this history
 *   4. regression  — no edge type vanished or collapsed since the last revision
 *
 * The regression check is the one that earns its keep. The analysis pipeline
 * reports edge removals as routine corrections, so a run that drops every
 * C++ `tested_by` edge, or every `defines_schema` edge pointing into a
 * re-analyzed file, still produces a graph that parses and validates. Only a
 * comparison against the previous revision separates cleanup from damage.
 *
 * Usage: node scripts/pages/verify-graph.mjs [--allow-regression]
 * Exits non-zero on failure. Warnings go to stderr and do not fail the run.
 */

import { execFileSync } from "node:child_process";
import { readFileSync, existsSync } from "node:fs";

const GRAPH_PATH = ".ua/knowledge-graph.json";
const IGNORE_PATH = ".ua/.understandignore";

// An edge type losing more than this fraction of its population is treated as
// a collapse. Regeneration legitimately moves counts around — summaries get
// rewritten, a refactor removes call sites — but a halving is a different
// kind of event, and the cost of a false positive is one maintainer override.
const COLLAPSE_THRESHOLD = 0.5;

const allowRegression =
  process.argv.includes("--allow-regression") ||
  process.env.ALLOW_GRAPH_REGRESSION === "true";

const problems = [];
const warnings = [];

function fail(message) {
  problems.push(message);
}

function warn(message) {
  warnings.push(message);
}

function git(args) {
  return execFileSync("git", args, { encoding: "utf8" }).trim();
}

/**
 * Read a path at a given revision, or null when it does not exist there.
 *
 * maxBuffer is raised deliberately: execFileSync defaults to 1 MB and the
 * graph is already larger than that, so the default silently turned every
 * regression comparison into "no previous revision found".
 */
function gitShow(revision, path) {
  try {
    return execFileSync("git", ["show", `${revision}:${path}`], {
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
      maxBuffer: 256 * 1024 * 1024,
    });
  } catch {
    return null;
  }
}

// --- 1. Shape ---------------------------------------------------------------

if (!existsSync(GRAPH_PATH)) {
  fail(`${GRAPH_PATH} does not exist — nothing to publish.`);
  report();
}

let graph;
try {
  graph = JSON.parse(readFileSync(GRAPH_PATH, "utf8"));
} catch (error) {
  fail(`${GRAPH_PATH} is not valid JSON: ${error.message}`);
  report();
}

for (const field of ["nodes", "edges"]) {
  if (!Array.isArray(graph[field])) {
    fail(`${GRAPH_PATH} has no ${field} array; the dashboard cannot render it.`);
  }
}
if (!graph.project || typeof graph.project !== "object") {
  fail(`${GRAPH_PATH} has no project metadata.`);
}
if (problems.length > 0) report();

// `layers` and `tour` are what make the published site navigable rather than a
// node soup. Their absence is survivable, so warn rather than block.
for (const field of ["layers", "tour"]) {
  if (!Array.isArray(graph[field]) || graph[field].length === 0) {
    warn(`${GRAPH_PATH} has no ${field}; the site will be less navigable.`);
  }
}

// --- 2. Excluded paths ------------------------------------------------------

/**
 * Conservative reading of .understandignore: literal directory and file
 * patterns only. Negations and character classes are skipped rather than
 * half-implemented — a missed exclusion is caught by the gitleaks scan, but a
 * wrongly-inferred one would block a legitimate deploy.
 */
function excludedPrefixes() {
  if (!existsSync(IGNORE_PATH)) return [];
  return readFileSync(IGNORE_PATH, "utf8")
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line && !line.startsWith("#") && !line.startsWith("!"))
    .filter((line) => !line.includes("*") && !line.includes("["))
    .map((line) => line.replace(/^\/+/, ""))
    .filter(Boolean);
}

const prefixes = excludedPrefixes();
const leaked = new Set();
for (const node of graph.nodes) {
  const filePath = node?.filePath;
  if (typeof filePath !== "string") continue;
  for (const prefix of prefixes) {
    const isDir = prefix.endsWith("/");
    if (isDir ? filePath.startsWith(prefix) : filePath === prefix) {
      leaked.add(`${filePath} (excluded by "${prefix}")`);
    }
  }
}
for (const entry of leaked) {
  fail(`Node references an excluded path: ${entry}`);
}

// --- 3. Provenance ----------------------------------------------------------

const graphCommit = graph.project?.gitCommitHash;
if (typeof graphCommit !== "string" || !/^[0-9a-f]{7,40}$/i.test(graphCommit)) {
  fail(
    `project.gitCommitHash is missing or malformed (${JSON.stringify(graphCommit)}); ` +
      "the site cannot state which commit the graph describes.",
  );
} else {
  let resolved = null;
  try {
    resolved = git(["rev-parse", "--verify", `${graphCommit}^{commit}`]);
  } catch {
    fail(
      `project.gitCommitHash ${graphCommit} is not a commit in this repository. ` +
        "Refusing to publish a graph that cannot be traced to history.",
    );
  }

  if (resolved) {
    const head = git(["rev-parse", "HEAD"]);
    if (resolved !== head) {
      let behind = "an unknown number of";
      try {
        behind = git(["rev-list", "--count", `${resolved}..${head}`]);
      } catch {
        /* shallow clone; the count is cosmetic */
      }
      warn(
        `Graph describes ${resolved.slice(0, 7)}, deploying ${head.slice(0, 7)} ` +
          `(${behind} commits later). The site will report the graph's commit, not HEAD.`,
      );
    }
  }
}

// --- 4. Regression ----------------------------------------------------------

function edgeHistogram(g) {
  const counts = new Map();
  for (const edge of g.edges ?? []) {
    counts.set(edge.type, (counts.get(edge.type) ?? 0) + 1);
  }
  return counts;
}

// The revision before the most recent one that touched the graph. Using file
// history rather than HEAD~1 means the comparison is against the last graph
// actually published, not against an unrelated intervening commit.
let previousGraph = null;
try {
  const revisions = git(["log", "-2", "--format=%H", "--", GRAPH_PATH])
    .split("\n")
    .filter(Boolean);
  if (revisions.length === 2) {
    const raw = gitShow(revisions[1], GRAPH_PATH);
    if (raw) previousGraph = JSON.parse(raw);
  }
} catch {
  /* no history available; handled below */
}

if (!previousGraph) {
  warn("No previous revision of the graph found; skipping the regression check.");
} else {
  const before = edgeHistogram(previousGraph);
  const after = edgeHistogram(graph);
  const regressions = [];

  for (const [type, wasCount] of before) {
    const nowCount = after.get(type) ?? 0;
    if (nowCount === 0) {
      regressions.push(`edge type "${type}" disappeared (was ${wasCount})`);
    } else if (nowCount < wasCount * (1 - COLLAPSE_THRESHOLD)) {
      regressions.push(`edge type "${type}" collapsed: ${wasCount} -> ${nowCount}`);
    }
  }

  const nodesBefore = previousGraph.nodes?.length ?? 0;
  const nodesAfter = graph.nodes.length;
  if (nodesAfter < nodesBefore * (1 - COLLAPSE_THRESHOLD)) {
    regressions.push(`node count collapsed: ${nodesBefore} -> ${nodesAfter}`);
  }

  if (regressions.length > 0) {
    const detail = regressions.map((r) => `  - ${r}`).join("\n");
    if (allowRegression) {
      warn(`Regression check overridden:\n${detail}`);
    } else {
      fail(
        "Graph regression detected against the previously published graph:\n" +
          detail +
          "\n  If this is a deliberate scope reduction, re-run with the override.",
      );
    }
  }
}

// --- Report -----------------------------------------------------------------

function report() {
  for (const warning of warnings) {
    process.stderr.write(`warning: ${warning}\n`);
  }
  if (problems.length > 0) {
    for (const problem of problems) {
      process.stderr.write(`error: ${problem}\n`);
    }
    process.exit(1);
  }
  process.stdout.write(
    `Graph verified: ${graph?.nodes?.length ?? 0} nodes, ` +
      `${graph?.edges?.length ?? 0} edges, ` +
      `${graph?.layers?.length ?? 0} layers, ` +
      `${graph?.tour?.length ?? 0} tour steps.\n`,
  );
  process.exit(0);
}

report();
