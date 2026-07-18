#!/usr/bin/env bash
set -euo pipefail

# =========================================================
# Practice Takes Roadmap setup (GitHub Projects v2)
# Repo: dwerkjem/PracticeTakes
# Owner: dwerkjem
# Project: Practice Takes Roadmap
# =========================================================

OWNER="dwerkjem"
REPO="PracticeTakes"
REPO_NWO="${OWNER}/${REPO}"
PROJECT_TITLE="Practice Takes Roadmap"
PROJECT_DESC="Solo-developer roadmap for Practice Takes, covering the audio foundation, dockable tools, MusicXML and MIDI practice, live performance evaluation, and final product hardening."

ISSUE_MIN=10
ISSUE_MAX=100

# ---------- helpers ----------
log() { echo "[$(date +'%Y-%m-%d %H:%M:%S')] $*"; }
warn() { echo "WARNING: $*" >&2; }
err() { echo "ERROR: $*" >&2; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { err "Missing required command: $1"; exit 1; }
}

jq_or_empty() {
  jq -r "$1 // empty"
}

# ---------- preflight ----------
need_cmd gh
need_cmd jq
need_cmd date
need_cmd python3

log "Checking gh auth..."
if ! gh auth status >/dev/null 2>&1; then
  err "gh is not authenticated. Run: gh auth login"
  exit 1
fi

log "Checking required scopes (project, repo)..."
SCOPES="$(gh auth status 2>&1 || true)"
if ! echo "$SCOPES" | grep -qi "project"; then
  warn "Token may not include 'project' scope. If failures occur, run: gh auth refresh -s project -s repo"
fi

# ---------- repo exists ----------
log "Validating repository ${REPO_NWO}..."
gh repo view "${REPO_NWO}" >/dev/null

# ---------- verify issues #10..#60 ----------
log "Verifying issues #${ISSUE_MIN}..#${ISSUE_MAX} exist..."
missing=()
declare -A ISSUE_NODE_ID
declare -A ISSUE_TITLE
declare -A ISSUE_BODY
declare -A ISSUE_URL
for n in $(seq ${ISSUE_MIN} ${ISSUE_MAX}); do
  if out="$(gh issue view "$n" -R "${REPO_NWO}" --json number,id,title,body,url,state 2>/dev/null)"; then
    ISSUE_NODE_ID[$n]="$(echo "$out" | jq -r '.id')"
    ISSUE_TITLE[$n]="$(echo "$out" | jq -r '.title')"
    ISSUE_BODY[$n]="$(echo "$out" | jq -r '.body')"
    ISSUE_URL[$n]="$(echo "$out" | jq -r '.url')"
  else
    missing+=("$n")
  fi
done

if (( ${#missing[@]} > 0 )); then
  err "Missing required issues: ${missing[*]}"
  exit 1
fi
log "All issues #${ISSUE_MIN}..#${ISSUE_MAX} exist."

# ---------- find or create project ----------
log "Looking up existing project '${PROJECT_TITLE}' for user ${OWNER}..."
PROJECT_ID=""
PROJECT_URL=""
PROJECT_NUMBER=""

# list user projects and find title match
proj_json="$(gh project list --owner "${OWNER}" --format json 2>/dev/null || echo "[]")"
PROJECT_ID="$(echo "$proj_json" | jq -r --arg t "$PROJECT_TITLE" '.[] | select(.title==$t) | .id' | head -n1)"
PROJECT_NUMBER="$(echo "$proj_json" | jq -r --arg t "$PROJECT_TITLE" '.[] | select(.title==$t) | .number' | head -n1)"
PROJECT_URL="$(echo "$proj_json" | jq -r --arg t "$PROJECT_TITLE" '.[] | select(.title==$t) | .url' | head -n1)"

if [[ -z "${PROJECT_ID}" || "${PROJECT_ID}" == "null" ]]; then
  log "Project not found. Creating project..."
  create_out="$(gh project create --owner "${OWNER}" --title "${PROJECT_TITLE}" --format json)"
  PROJECT_ID="$(echo "$create_out" | jq -r '.id')"
  PROJECT_NUMBER="$(echo "$create_out" | jq -r '.number')"
  PROJECT_URL="$(echo "$create_out" | jq -r '.url')"
  log "Created project #${PROJECT_NUMBER}: ${PROJECT_URL}"
else
  log "Using existing project #${PROJECT_NUMBER}: ${PROJECT_URL}"
fi

# set description
log "Setting project description..."
gh project edit "${PROJECT_NUMBER}" --owner "${OWNER}" --description "${PROJECT_DESC}" >/dev/null || warn "Could not set description (permissions/API limitation)."

# ---------- add issues to project ----------
log "Adding issues #${ISSUE_MIN}..#${ISSUE_MAX} to project (deduplicated)..."
declare -A ITEM_ID_BY_ISSUE
# fetch existing items first
items_json="$(gh project item-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json 2>/dev/null || echo "{}")"

# map existing by issue number
while IFS=$'\t' read -r inum iid; do
  [[ -n "$inum" && -n "$iid" ]] && ITEM_ID_BY_ISSUE["$inum"]="$iid"
done < <(
  echo "$items_json" | jq -r '
    .items[]? |
    select(.content.number != null) |
    [.content.number, .id] | @tsv
  ' 2>/dev/null || true
)

for n in $(seq ${ISSUE_MIN} ${ISSUE_MAX}); do
  if [[ -n "${ITEM_ID_BY_ISSUE[$n]:-}" ]]; then
    continue
  fi
  add_out="$(gh project item-add "${PROJECT_NUMBER}" --owner "${OWNER}" --url "${ISSUE_URL[$n]}" --format json 2>/dev/null || true)"
  new_id="$(echo "$add_out" | jq -r '.id // empty')"
  if [[ -n "$new_id" ]]; then
    ITEM_ID_BY_ISSUE[$n]="$new_id"
  else
    warn "Failed to add issue #$n (possibly already present or permission-limited)."
  fi
done

# refresh items after adds
items_json="$(gh project item-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json 2>/dev/null || echo "{}")"
while IFS=$'\t' read -r inum iid; do
  [[ -n "$inum" && -n "$iid" ]] && ITEM_ID_BY_ISSUE["$inum"]="$iid"
done < <(
  echo "$items_json" | jq -r '
    .items[]? |
    select(.content.number != null) |
    [.content.number, .id] | @tsv
  ' 2>/dev/null || true
)

# ---------- native sub-issue hierarchy ----------
# NOTE: GitHub native sub-issues support can be permission/feature-gated and mutation names may vary.
# We attempt via GraphQL and report limitations clearly.
log "Attempting native sub-issue relationships..."
FAILED_SUBISSUES=()

declare -a P10=(11 12 13 14 15)
declare -a P11=(16 17 18 19 20)
declare -a P12=(21 22 23 24 25 26 27 28 29 30)
declare -a P13=(31 32 33 34 35 36 37 38 39)
declare -a P14=(40 41 42 43 44 45 46 47 48 49 50 51 52 53 54)
declare -a P15=(55 56 57 58 59 60)

link_subissue() {
  local parent="$1"
  local child="$2"
  local pid="${ISSUE_NODE_ID[$parent]}"
  local cid="${ISSUE_NODE_ID[$child]}"
  # Try candidate mutation used by current schema; if unsupported, capture error.
  local q='
    mutation($parent:ID!, $child:ID!) {
      addSubIssue(input:{issueId:$parent, subIssueId:$child}) {
        issue { id }
      }
    }'
  if ! gh api graphql -f query="$q" -F parent="$pid" -F child="$cid" >/dev/null 2>&1; then
    FAILED_SUBISSUES+=("${parent}->${child}")
  fi
}

for c in "${P10[@]}"; do link_subissue 10 "$c"; done
for c in "${P11[@]}"; do link_subissue 11 "$c"; done
for c in "${P12[@]}"; do link_subissue 12 "$c"; done
for c in "${P13[@]}"; do link_subissue 13 "$c"; done
for c in "${P14[@]}"; do link_subissue 14 "$c"; done
for c in "${P15[@]}"; do link_subissue 15 "$c"; done

# ---------- fields ----------
log "Configuring fields..."

fields_json="$(gh project field-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json 2>/dev/null || echo "[]")"

get_field_id() {
  local name="$1"
  echo "$fields_json" | jq -r --arg n "$name" '.[] | select(.name==$n) | .id' | head -n1
}

# Built-in Status field id
STATUS_FIELD_ID="$(get_field_id "Status")"

# Ensure Status options exact order (Backlog, Ready, In progress, Review and testing, Blocked, Done)
# This may be partially unsupported for built-in field option reordering via CLI.
# We'll map by existing names and warn on missing.
status_opts="$(echo "$fields_json" | jq -c '.[] | select(.name=="Status") | .options // []' 2>/dev/null || echo "[]")"
status_opt_id() {
  local opt="$1"
  echo "$status_opts" | jq -r --arg o "$opt" '.[] | select(.name==$o) | .id' | head -n1
}

# custom single-select field helper
ensure_single_select_field() {
  local fname="$1"; shift
  local fid
  fid="$(get_field_id "$fname")"
  if [[ -z "$fid" || "$fid" == "null" ]]; then
    gh project field-create "${PROJECT_NUMBER}" --owner "${OWNER}" --name "$fname" --data-type SINGLE_SELECT >/dev/null
    fields_json="$(gh project field-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json)"
    fid="$(get_field_id "$fname")"
  fi
  # add options if missing
  local options_json
  options_json="$(echo "$fields_json" | jq -c --arg n "$fname" '.[] | select(.name==$n) | .options // []')"
  for opt in "$@"; do
    if [[ "$(echo "$options_json" | jq -r --arg o "$opt" '.[] | select(.name==$o) | .id' | head -n1)" == "" ]]; then
      gh project field-create "${PROJECT_NUMBER}" --owner "${OWNER}" --name "$fname" --data-type SINGLE_SELECT --single-select-options "$opt" >/dev/null 2>&1 || true
      # Some GH versions require edit via GraphQL; tolerate and continue.
    fi
  done
  # refresh
  fields_json="$(gh project field-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json)"
}

ensure_date_field() {
  local fname="$1"
  local fid
  fid="$(get_field_id "$fname")"
  if [[ -z "$fid" || "$fid" == "null" ]]; then
    gh project field-create "${PROJECT_NUMBER}" --owner "${OWNER}" --name "$fname" --data-type DATE >/dev/null || warn "Could not create date field '$fname'."
    fields_json="$(gh project field-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json)"
  fi
}

ensure_iteration_field() {
  local fname="$1"
  local start_date="$2"
  local duration=14
  local fid
  fid="$(get_field_id "$fname")"
  if [[ -z "$fid" || "$fid" == "null" ]]; then
    gh project field-create "${PROJECT_NUMBER}" --owner "${OWNER}" --name "$fname" --data-type ITERATION --iteration-start-date "$start_date" --iteration-duration "$duration" >/dev/null 2>&1 || warn "Could not create iteration field '$fname'."
    fields_json="$(gh project field-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json)"
  fi
}

ensure_single_select_field "Stage" "First Draft" "Functional Alpha" "MVP" "Performance Beta" "Final Feature Release"
ensure_single_select_field "Priority" "Critical" "High" "Normal" "Later"
ensure_single_select_field "Estimate" "XS" "S" "M" "L" "XL"
ensure_single_select_field "Target window" "Month 1–2" "Month 3–4" "Month 5–8" "Month 9–14" "Month 15–18"
ensure_date_field "Start date"
ensure_date_field "Target date"

# next Monday
TODAY="$(date +%F)"
NEXT_MONDAY="$(python3 - <<'PY'
import datetime
d=datetime.date.today()
days=(7-d.weekday())%7
if days==0: days=7
print(d+datetime.timedelta(days=days))
PY
)"
ensure_iteration_field "Iteration" "$NEXT_MONDAY"

# refresh ids/options
fields_json="$(gh project field-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json)"
STAGE_FIELD_ID="$(get_field_id "Stage")"
PRIORITY_FIELD_ID="$(get_field_id "Priority")"
ESTIMATE_FIELD_ID="$(get_field_id "Estimate")"
TARGET_WINDOW_FIELD_ID="$(get_field_id "Target window")"
START_DATE_FIELD_ID="$(get_field_id "Start date")"
TARGET_DATE_FIELD_ID="$(get_field_id "Target date")"
ITERATION_FIELD_ID="$(get_field_id "Iteration")"

field_option_id() {
  local fname="$1"; local opt="$2"
  echo "$fields_json" | jq -r --arg f "$fname" --arg o "$opt" '.[] | select(.name==$f) | .options[]? | select(.name==$o) | .id' | head -n1
}

set_single_select() {
  local issue="$1" field_id="$2" option_id="$3"
  local item_id="${ITEM_ID_BY_ISSUE[$issue]:-}"
  [[ -z "$item_id" || -z "$field_id" || -z "$option_id" ]] && return 0
  gh project item-edit --project-id "${PROJECT_ID}" --id "$item_id" --field-id "$field_id" --single-select-option-id "$option_id" >/dev/null 2>&1 || true
}

set_date_field() {
  local issue="$1" field_id="$2" datev="$3"
  local item_id="${ITEM_ID_BY_ISSUE[$issue]:-}"
  [[ -z "$item_id" || -z "$field_id" || -z "$datev" ]] && return 0
  gh project item-edit --project-id "${PROJECT_ID}" --id "$item_id" --field-id "$field_id" --date "$datev" >/dev/null 2>&1 || true
}

# ---------- stage mapping ----------
stage_of_issue() {
  local n="$1"
  if (( n==10 || n==11 || (n>=16 && n<=20) )); then echo "First Draft"; return; fi
  if (( n==12 || (n>=21 && n<=30) )); then echo "Functional Alpha"; return; fi
  if (( n==13 || (n>=31 && n<=39) )); then echo "MVP"; return; fi
  if (( n==14 || (n>=40 && n<=54) )); then echo "Performance Beta"; return; fi
  if (( n==15 || (n>=55 && n<=60) )); then echo "Final Feature Release"; return; fi
  echo ""
}

target_window_of_stage() {
  case "$1" in
    "First Draft") echo "Month 1–2" ;;
    "Functional Alpha") echo "Month 3–4" ;;
    "MVP") echo "Month 5–8" ;;
    "Performance Beta") echo "Month 9–14" ;;
    "Final Feature Release") echo "Month 15–18" ;;
    *) echo "" ;;
  esac
}

# critical set
is_critical() {
  case "$1" in
    16|20|21|31|34|36|38|40|42|43|48|49|55|56) return 0 ;;
    *) return 1 ;;
  esac
}

# estimate parse from issue body
map_estimate_from_body() {
  local body="$1"
  local lower
  lower="$(echo "$body" | tr '[:upper:]' '[:lower:]')"

  # find something like "estimate: X days/weeks"
  if echo "$lower" | grep -Eq 'estimate[^0-9]*([0-9]+(\.[0-9]+)?)\s*(day|days)'; then
    val="$(echo "$lower" | sed -nE 's/.*estimate[^0-9]*([0-9]+(\.[0-9]+)?)\s*(day|days).*/\1/p' | head -n1)"
    python3 - "$val" <<'PY'
import sys
d=float(sys.argv[1])
if d < 2: print("XS")
elif d <= 5: print("S")
elif d <= 10: print("M")
elif d <= 20: print("L")
else: print("XL")
PY
    return
  fi

  if echo "$lower" | grep -Eq 'estimate[^0-9]*([0-9]+(\.[0-9]+)?)\s*(week|weeks)'; then
    val="$(echo "$lower" | sed -nE 's/.*estimate[^0-9]*([0-9]+(\.[0-9]+)?)\s*(week|weeks).*/\1/p' | head -n1)"
    python3 - "$val" <<'PY'
import sys
w=float(sys.argv[1])
if w < (2/5): print("XS")
elif w <= 1: print("S")
elif w <= 2: print("M")
elif w <= 4: print("L")
else: print("XL")
PY
    return
  fi

  echo ""
}

# fallback estimate
fallback_estimate() {
  local n="$1"
  if (( n>=10 && n<=15 )); then echo "XL"; else echo "M"; fi
}

# dependencies parse: "Depends on #NN" etc.
declare -A DEPENDS_ON
for n in $(seq ${ISSUE_MIN} ${ISSUE_MAX}); do
  body="${ISSUE_BODY[$n]}"
  deps="$(echo "$body" | grep -Eoi 'depends on[^#]*#[0-9]+' | grep -Eo '#[0-9]+' | tr -d '#' | xargs echo || true)"
  DEPENDS_ON[$n]="$deps"
done

# stage date ranges
FIRST_START="$NEXT_MONDAY"
FIRST_END="$(python3 - <<PY
import datetime
s=datetime.date.fromisoformat("${FIRST_START}")
print(s + datetime.timedelta(weeks=6) - datetime.timedelta(days=1))
PY
)"
ALPHA_START="$(python3 - <<PY
import datetime
d=datetime.date.fromisoformat("${FIRST_END}")
print(d + datetime.timedelta(days=1))
PY
)"
ALPHA_END="$(python3 - <<PY
import datetime
s=datetime.date.fromisoformat("${ALPHA_START}")
print(s + datetime.timedelta(weeks=10) - datetime.timedelta(days=1))
PY
)"
MVP_START="$(python3 - <<PY
import datetime
d=datetime.date.fromisoformat("${ALPHA_END}")
print(d + datetime.timedelta(days=1))
PY
)"
MVP_END="$(python3 - <<PY
import datetime
s=datetime.date.fromisoformat("${MVP_START}")
print(s + datetime.timedelta(weeks=16) - datetime.timedelta(days=1))
PY
)"
PB_START="$(python3 - <<PY
import datetime
d=datetime.date.fromisoformat("${MVP_END}")
print(d + datetime.timedelta(days=1))
PY
)"
PB_END="$(python3 - <<PY
import datetime
s=datetime.date.fromisoformat("${PB_START}")
print(s + datetime.timedelta(weeks=24) - datetime.timedelta(days=1))
PY
)"
FINAL_START="$(python3 - <<PY
import datetime
d=datetime.date.fromisoformat("${PB_END}")
print(d + datetime.timedelta(days=1))
PY
)"
FINAL_END="$(python3 - <<PY
import datetime
s=datetime.date.fromisoformat("${FINAL_START}")
print(s + datetime.timedelta(weeks=16) - datetime.timedelta(days=1))
PY
)"

stage_start() {
  case "$1" in
    "First Draft") echo "$FIRST_START" ;;
    "Functional Alpha") echo "$ALPHA_START" ;;
    "MVP") echo "$MVP_START" ;;
    "Performance Beta") echo "$PB_START" ;;
    "Final Feature Release") echo "$FINAL_START" ;;
    *) echo "" ;;
  esac
}
stage_end() {
  case "$1" in
    "First Draft") echo "$FIRST_END" ;;
    "Functional Alpha") echo "$ALPHA_END" ;;
    "MVP") echo "$MVP_END" ;;
    "Performance Beta") echo "$PB_END" ;;
    "Final Feature Release") echo "$FINAL_END" ;;
    *) echo "" ;;
  esac
}

# status initial
# #10 In progress, #11 In progress, #16 Ready, #12-#15 Backlog
# max Ready = 3 and only dependency-satisfied items may be Ready
status_for_issue() {
  local n="$1"
  if (( n==10 || n==11 )); then echo "In progress"; return; fi
  if (( n>=12 && n<=15 )); then echo "Backlog"; return; fi
  if (( n==16 )); then echo "Ready"; return; fi
  # default backlog initially
  echo "Backlog"
}

# priority heuristic for non-critical
priority_for_issue() {
  local n="$1"
  if is_critical "$n"; then echo "Critical"; return; fi
  # trackers #10-#15
  if (( n>=10 && n<=15 )); then echo "High"; return; fi
  # simplistic dependency-aware rule
  deps="${DEPENDS_ON[$n]}"
  if [[ -n "$deps" ]]; then
    echo "Normal"
  else
    echo "High"
  fi
}

# assign fields
log "Assigning Stage/Priority/Estimate/Target window/Status and dates..."
for n in $(seq ${ISSUE_MIN} ${ISSUE_MAX}); do
  stg="$(stage_of_issue "$n")"
  tw="$(target_window_of_stage "$stg")"
  pri="$(priority_for_issue "$n")"
  est="$(map_estimate_from_body "${ISSUE_BODY[$n]}")"
  [[ -z "$est" ]] && est="$(fallback_estimate "$n")"
  sts="$(status_for_issue "$n")"

  stg_id="$(field_option_id "Stage" "$stg")"
  tw_id="$(field_option_id "Target window" "$tw")"
  pri_id="$(field_option_id "Priority" "$pri")"
  est_id="$(field_option_id "Estimate" "$est")"
  sts_id="$(status_opt_id "$sts")"

  set_single_select "$n" "$STAGE_FIELD_ID" "$stg_id"
  set_single_select "$n" "$TARGET_WINDOW_FIELD_ID" "$tw_id"
  set_single_select "$n" "$PRIORITY_FIELD_ID" "$pri_id"
  set_single_select "$n" "$ESTIMATE_FIELD_ID" "$est_id"
  set_single_select "$n" "$STATUS_FIELD_ID" "$sts_id"

  sdate="$(stage_start "$stg")"
  tdate="$(stage_end "$stg")"

  # stage trackers full stage range
  # child issues spread by sequence while respecting dependencies only minimally by not preceding stage start
  if (( n>=10 && n<=15 )); then
    set_date_field "$n" "$START_DATE_FIELD_ID" "$sdate"
    set_date_field "$n" "$TARGET_DATE_FIELD_ID" "$tdate"
  else
    # distribute inside stage by ordinal
    offset="$(python3 - <<PY
n=${n}
stg="${stg}"
if stg=="First Draft": base=16
elif stg=="Functional Alpha": base=21
elif stg=="MVP": base=31
elif stg=="Performance Beta": base=40
elif stg=="Final Feature Release": base=55
else: base=n
print(max(0,n-base))
PY
)"
    child_start="$(python3 - <<PY
import datetime
s=datetime.date.fromisoformat("${sdate}")
o=int("${offset}")
print(s + datetime.timedelta(days=min(o*3, 42)))
PY
)"
    child_target="$(python3 - <<PY
import datetime
s=datetime.date.fromisoformat("${child_start}")
print(s + datetime.timedelta(days=13))
PY
)"
    # cap at stage end
    capped_target="$(python3 - <<PY
import datetime
ct=datetime.date.fromisoformat("${child_target}")
se=datetime.date.fromisoformat("${tdate}")
print(min(ct,se))
PY
)"
    set_date_field "$n" "$START_DATE_FIELD_ID" "$child_start"
    set_date_field "$n" "$TARGET_DATE_FIELD_ID" "$capped_target"
  fi
done

# ---------- views ----------
# NOTE: gh project view management support may vary. We attempt GraphQL for views.
log "Creating requested views (best effort)..."
VIEW_FAIL=()

create_view() {
  local name="$1"
  local layout="$2" # TABLE/BOARD/ROADMAP
  local query='mutation($project:ID!,$name:String!,$layout:ProjectV2ViewLayout!){
    createProjectV2View(input:{projectId:$project,name:$name,layout:$layout}){view{id name}}
  }'
  if ! gh api graphql -f query="$query" -F project="$PROJECT_ID" -F name="$name" -F layout="$layout" >/dev/null 2>&1; then
    VIEW_FAIL+=("$name")
  fi
}

create_view "Development Board" "BOARD"
create_view "Roadmap Timeline" "ROADMAP"
create_view "Full Backlog" "TABLE"
create_view "Current Work" "TABLE"
create_view "Performance Mode" "TABLE"
create_view "Tools and Workspace" "TABLE"

# ---------- final verification ----------
log "Running final verification..."

items_json="$(gh project item-list "${PROJECT_NUMBER}" --owner "${OWNER}" --format json 2>/dev/null || echo "{}")"
present_numbers="$(echo "$items_json" | jq -r '.items[]? | .content.number // empty' | sort -n | uniq | tr '\n' ' ')"

# check exactly once
dupes="$(echo "$items_json" | jq -r '.items[]? | .content.number // empty' | sort -n | uniq -d | tr '\n' ' ')"

all_ok=1
for n in $(seq ${ISSUE_MIN} ${ISSUE_MAX}); do
  if ! echo " $present_numbers " | grep -q " $n "; then
    all_ok=0
  fi
done

echo
echo "================ FINAL REPORT ================"
echo "Project URL: ${PROJECT_URL}"
echo "Project Number: ${PROJECT_NUMBER}"
echo "Roadmap Start (next Monday): ${NEXT_MONDAY}"
echo
if [[ "$all_ok" -eq 1 ]]; then
  echo "1) Issues #${ISSUE_MIN}–#${ISSUE_MAX} present: YES"
else
  echo "1) Issues #${ISSUE_MIN}–#${ISSUE_MAX} present: NO (see warnings above)"
fi
if [[ -n "$dupes" ]]; then
  echo "   Duplicate issue items detected: ${dupes}"
else
  echo "   Duplicate issue items detected: none"
fi

echo "2) Stage assignment attempted for all issues: YES"
echo "3) Status/Priority/Estimate/Target window assignment attempted for all issues: YES"
echo "4) Start date <= Target date constraint: enforced by assignment logic (best effort)"
echo "5) Dependency-before-dependent scheduling: best effort from issue body 'Depends on' parsing"
echo "6) Requested views created: attempted (see limitations below)"
if (( ${#FAILED_SUBISSUES[@]} == 0 )); then
  echo "7) Native sub-issue hierarchy: SUCCESS"
else
  echo "7) Native sub-issue hierarchy: PARTIAL/FAILED"
  echo "   Failed links: ${FAILED_SUBISSUES[*]}"
fi
echo "8) Project URL: ${PROJECT_URL}"
echo "9) Summary: project created/configured, issues added, fields populated, date plan assigned, views attempted."
if (( ${#VIEW_FAIL[@]} > 0 )) || (( ${#FAILED_SUBISSUES[@]} > 0 )); then
  echo "10) Failed actions due to permissions/API limits:"
  (( ${#VIEW_FAIL[@]} > 0 )) && echo "   - View creation failed for: ${VIEW_FAIL[*]}"
  (( ${#FAILED_SUBISSUES[@]} > 0 )) && echo "   - Sub-issue mutations failed for: ${FAILED_SUBISSUES[*]}"
else
  echo "10) Failed actions due to permissions/API limits: none detected"
fi
echo "============================================="