export interface AdminEnv {
  FEEDBACK_DB: D1Database;
  ADMIN_EMAILS: string;
}

interface SubmissionRow {
  receipt_id: string;
  submitted_at: string;
  received_at: number;
  app_version: string;
  category: string;
  message: string;
  contact_email: string | null;
  status: string;
  developer_notes: string;
  priority: string | null;
  tags_json: string;
  github_issue_url: string | null;
  duplicate_of: string | null;
}

const statuses = new Set(["new", "needs_review", "planned", "duplicate", "resolved", "declined"]);
const priorities = new Set(["low", "medium", "high", "critical"]);
const jsonHeaders = { "content-type": "application/json; charset=utf-8" };

export async function handleAdminRequest(request: Request, env: AdminEnv): Promise<Response> {
  const user = authenticatedUser(request, env.ADMIN_EMAILS);
  if (!user) {
    return new Response("Administrative access requires an authorized Cloudflare Access identity.", {
      status: 401,
      headers: { "cache-control": "no-store", "content-type": "text/plain; charset=utf-8" },
    });
  }

  const url = new URL(request.url);
  if (url.pathname === "/admin" && request.method === "GET") {
    return new Response(adminPage(user), {
      headers: {
        "cache-control": "no-store",
        "content-security-policy": "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; frame-ancestors 'none'",
        "content-type": "text/html; charset=utf-8",
        "x-content-type-options": "nosniff",
      },
    });
  }
  if (url.pathname === "/v1/admin/submissions" && request.method === "GET") {
    return listSubmissions(url, env.FEEDBACK_DB);
  }
  if (url.pathname === "/v1/admin/export" && request.method === "GET") {
    return exportSubmissions(url, env.FEEDBACK_DB);
  }
  const match = url.pathname.match(/^\/v1\/admin\/submissions\/([0-9a-f-]{36})$/i);
  if (match && request.method === "PATCH") {
    return updateSubmission(match[1], request, env.FEEDBACK_DB);
  }
  return jsonResponse(404, { error: { code: "not_found", message: "Administrative route not found." } });
}

function authenticatedUser(request: Request, configuredEmails: string): string | null {
  const email = request.headers.get("cf-access-authenticated-user-email")?.trim().toLowerCase();
  const allowed = configuredEmails.split(",").map((value) => value.trim().toLowerCase()).filter(Boolean);
  return email && allowed.includes(email) ? email : null;
}

async function listSubmissions(url: URL, db: D1Database): Promise<Response> {
  const { where, values } = filters(url);
  const limit = Math.min(Math.max(Number.parseInt(url.searchParams.get("limit") ?? "100", 10) || 100, 1), 250);
  const result = await db.prepare(
    `SELECT receipt_id, submitted_at, received_at, app_version, category, message, contact_email,
            status, developer_notes, priority, tags_json, github_issue_url, duplicate_of
       FROM feedback_submissions ${where} ORDER BY received_at DESC LIMIT ?`,
  ).bind(...values, limit).all<SubmissionRow>();
  return jsonResponse(200, { submissions: (result.results ?? []).map(presentSubmission) });
}

function filters(url: URL): { where: string; values: unknown[] } {
  const clauses: string[] = [];
  const values: unknown[] = [];
  const addExact = (parameter: string, column: string) => {
    const value = url.searchParams.get(parameter)?.trim();
    if (value) { clauses.push(`${column} = ?`); values.push(value); }
  };
  addExact("category", "category");
  addExact("appVersion", "app_version");
  addExact("status", "status");
  addExact("priority", "priority");
  const query = url.searchParams.get("q")?.trim();
  if (query) {
    clauses.push("(message LIKE ? OR developer_notes LIKE ? OR tags_json LIKE ? OR contact_email LIKE ?)");
    const pattern = `%${query.replace(/[\\%_]/g, "\\$&")}%`;
    values.push(pattern, pattern, pattern, pattern);
  }
  const platform = url.searchParams.get("platform")?.trim();
  if (platform) { clauses.push("message LIKE ?"); values.push(`%Environment:%${platform}%`); }
  const from = Date.parse(url.searchParams.get("from") ?? "");
  if (Number.isFinite(from)) { clauses.push("received_at >= ?"); values.push(Math.floor(from / 1000)); }
  const to = Date.parse(url.searchParams.get("to") ?? "");
  if (Number.isFinite(to)) { clauses.push("received_at <= ?"); values.push(Math.floor(to / 1000) + 86399); }
  return { where: clauses.length ? `WHERE ${clauses.join(" AND ")}` : "", values };
}

async function updateSubmission(receiptId: string, request: Request, db: D1Database): Promise<Response> {
  let input: unknown;
  try { input = await request.json(); } catch { return invalid("Request body must be valid JSON."); }
  if (!isRecord(input)) return invalid("Request body must be an object.");

  const updates: string[] = [];
  const values: unknown[] = [];
  const set = (column: string, value: unknown) => { updates.push(`${column} = ?`); values.push(value); };
  if (input.status !== undefined) {
    if (typeof input.status !== "string" || !statuses.has(input.status)) return invalid("Unknown status.");
    set("status", input.status);
  }
  if (input.priority !== undefined) {
    if (input.priority !== null && (typeof input.priority !== "string" || !priorities.has(input.priority))) {
      return invalid("Unknown priority.");
    }
    set("priority", input.priority);
  }
  if (input.developerNotes !== undefined) {
    if (typeof input.developerNotes !== "string" || input.developerNotes.length > 8000) return invalid("Developer notes are too long.");
    set("developer_notes", input.developerNotes.trim());
  }
  if (input.tags !== undefined) {
    if (!Array.isArray(input.tags) || input.tags.length > 20 ||
        input.tags.some((tag) => typeof tag !== "string" || tag.trim().length < 1 || tag.length > 40)) {
      return invalid("Tags must contain at most 20 values of 1 to 40 characters.");
    }
    set("tags_json", JSON.stringify([...new Set(input.tags.map((tag) => (tag as string).trim()))]));
  }
  if (input.githubIssueUrl !== undefined) {
    if (input.githubIssueUrl !== null &&
        (typeof input.githubIssueUrl !== "string" || !/^https:\/\/github\.com\/[^/]+\/[^/]+\/issues\/\d+$/.test(input.githubIssueUrl))) {
      return invalid("GitHub issue URL is invalid.");
    }
    set("github_issue_url", input.githubIssueUrl || null);
  }
  if (input.duplicateOf !== undefined) {
    if (input.duplicateOf !== null &&
        (typeof input.duplicateOf !== "string" || !/^[0-9a-f-]{36}$/i.test(input.duplicateOf) || input.duplicateOf === receiptId)) {
      return invalid("Duplicate target is invalid.");
    }
    set("duplicate_of", input.duplicateOf);
    if (input.duplicateOf) set("status", "duplicate");
  }
  if (!updates.length) return invalid("No supported fields were supplied.");

  values.push(receiptId);
  const result = await db.prepare(`UPDATE feedback_submissions SET ${updates.join(", ")} WHERE receipt_id = ?`)
    .bind(...values).run();
  if (!result.meta.changes) return jsonResponse(404, { error: { code: "not_found", message: "Submission not found." } });
  return jsonResponse(200, { receiptId, updated: true });
}

async function exportSubmissions(url: URL, db: D1Database): Promise<Response> {
  const ids = [...new Set(url.searchParams.getAll("id"))].filter((id) => /^[0-9a-f-]{36}$/i.test(id));
  if (!ids.length || ids.length > 250) return invalid("Select between 1 and 250 submissions to export.");
  const placeholders = ids.map(() => "?").join(",");
  const result = await db.prepare(
    `SELECT receipt_id, submitted_at, received_at, app_version, category, message, contact_email,
            status, developer_notes, priority, tags_json, github_issue_url, duplicate_of
       FROM feedback_submissions WHERE receipt_id IN (${placeholders}) ORDER BY received_at DESC`,
  ).bind(...ids).all<SubmissionRow>();
  const columns: (keyof SubmissionRow)[] = ["receipt_id", "submitted_at", "received_at", "app_version", "category",
    "message", "contact_email", "status", "developer_notes", "priority", "tags_json", "github_issue_url", "duplicate_of"];
  const csv = [columns.join(","), ...(result.results ?? []).map((row) =>
    columns.map((column) => csvCell(row[column])).join(","))].join("\r\n");
  return new Response(csv, { headers: {
    "cache-control": "no-store", "content-disposition": "attachment; filename=practice-takes-feedback.csv",
    "content-type": "text/csv; charset=utf-8",
  } });
}

function presentSubmission(row: SubmissionRow) {
  const marker = "\nEnvironment: ";
  const markerIndex = row.message.lastIndexOf(marker);
  const userFeedback = markerIndex < 0 ? row.message : row.message.slice(0, markerIndex);
  const diagnosticContext = markerIndex < 0 ? null : row.message.slice(markerIndex + marker.length).trim();
  let tags: unknown = [];
  try { tags = JSON.parse(row.tags_json); } catch { /* preserve an empty list for malformed legacy data */ }
  return {
    receiptId: row.receipt_id, submittedAt: row.submitted_at,
    receivedAt: new Date(row.received_at * 1000).toISOString(), appVersion: row.app_version,
    category: row.category, userFeedback, diagnosticContext, contactEmail: row.contact_email,
    status: row.status, developerNotes: row.developer_notes, priority: row.priority, tags,
    githubIssueUrl: row.github_issue_url, duplicateOf: row.duplicate_of,
  };
}

function csvCell(value: unknown): string {
  const text = value === null || value === undefined ? "" : String(value);
  return `"${text.replace(/"/g, '""')}"`;
}

function invalid(message: string): Response {
  return jsonResponse(400, { error: { code: "invalid_request", message } });
}

function jsonResponse(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), { status, headers: { ...jsonHeaders, "cache-control": "no-store" } });
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function adminPage(user: string): string {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Practice Takes feedback</title><style>
:root{font:15px system-ui;color:#18212b;background:#f4f6f8}*{box-sizing:border-box}body{margin:0}header{background:#18212b;color:white;padding:1rem 2rem;display:flex;justify-content:space-between}main{padding:1.5rem;max-width:1500px;margin:auto}.filters,.actions{display:flex;gap:.6rem;flex-wrap:wrap;margin-bottom:1rem}input,select,textarea,button{font:inherit;padding:.55rem;border:1px solid #aab4bf;border-radius:5px}button{cursor:pointer;background:#1769aa;color:white;border:0}.card{background:white;border:1px solid #d6dce2;border-radius:7px;margin:.8rem 0;padding:1rem}.meta{display:flex;gap:1rem;flex-wrap:wrap;color:#596773}.message{white-space:pre-wrap}.diagnostics{background:#eef2f5;padding:.7rem;white-space:pre-wrap}.editor{display:grid;grid-template-columns:repeat(4,minmax(130px,1fr));gap:.6rem;margin-top:1rem}.editor textarea{grid-column:span 2;min-height:75px}.editor .wide{grid-column:span 2}.empty{text-align:center;padding:3rem;color:#687685}@media(max-width:800px){.editor{grid-template-columns:1fr}.editor textarea,.editor .wide{grid-column:auto}}
</style></head><body><header><strong>Feedback triage</strong><span>${escapeHtml(user)}</span></header><main>
<form class="filters" id="filters"><input name="q" placeholder="Search feedback, notes, tags"><select name="category"><option value="">All categories</option><option>bug</option><option>idea</option><option>usability</option><option>other</option></select><input name="appVersion" placeholder="App version"><input name="platform" placeholder="Platform"><input name="from" type="date"><input name="to" type="date"><select name="status"><option value="">All statuses</option><option value="new">New</option><option value="needs_review">Needs review</option><option value="planned">Planned</option><option value="duplicate">Duplicate</option><option value="resolved">Resolved</option><option value="declined">Declined</option></select><button>Search</button></form>
<div class="actions"><button id="export" type="button">Export selected</button><span id="count"></span></div><section id="results" aria-live="polite"></section></main>
<script>
const results=document.querySelector('#results'),count=document.querySelector('#count'),form=document.querySelector('#filters');
const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function load(){results.innerHTML='<p class="empty">Loading…</p>';const q=new URLSearchParams(new FormData(form));for(const [k,v] of [...q])if(!v)q.delete(k);const r=await fetch('/v1/admin/submissions?'+q);const b=await r.json();if(!r.ok){results.textContent=b.error?.message||'Unable to load';return}count.textContent=b.submissions.length+' submissions';results.innerHTML=b.submissions.map(card).join('')||'<p class="empty">No matching feedback.</p>'}
function card(x){return '<article class="card" data-id="'+esc(x.receiptId)+'"><div class="meta"><input class="pick" type="checkbox" aria-label="Select"><strong>'+esc(x.category)+'</strong><span>'+esc(x.status)+'</span><span>'+esc(x.appVersion)+'</span><time>'+esc(new Date(x.receivedAt).toLocaleString())+'</time><code>'+esc(x.receiptId)+'</code></div><p class="message">'+esc(x.userFeedback)+'</p>'+(x.diagnosticContext?'<details><summary>Diagnostic context</summary><pre class="diagnostics">'+esc(x.diagnosticContext)+'</pre></details>':'')+'<div class="editor"><select class="status">'+['new','needs_review','planned','duplicate','resolved','declined'].map(v=>'<option '+(x.status===v?'selected':'')+' value="'+v+'">'+v.replace('_',' ')+'</option>').join('')+'</select><select class="priority"><option value="">No priority</option>'+['low','medium','high','critical'].map(v=>'<option '+(x.priority===v?'selected':'')+'>'+v+'</option>').join('')+'</select><input class="tags wide" value="'+esc((x.tags||[]).join(', '))+'" placeholder="Tags, comma separated"><input class="github wide" value="'+esc(x.githubIssueUrl||'')+'" placeholder="GitHub issue URL"><input class="duplicate wide" value="'+esc(x.duplicateOf||'')+'" placeholder="Duplicate of receipt ID"><textarea class="notes" placeholder="Developer notes">'+esc(x.developerNotes)+'</textarea><button class="save" type="button">Save</button></div></article>'}
form.addEventListener('submit',e=>{e.preventDefault();load()});results.addEventListener('click',async e=>{if(!e.target.classList.contains('save'))return;const c=e.target.closest('.card'),button=e.target;button.disabled=true;const body={status:c.querySelector('.status').value,priority:c.querySelector('.priority').value||null,tags:c.querySelector('.tags').value.split(',').map(v=>v.trim()).filter(Boolean),githubIssueUrl:c.querySelector('.github').value.trim()||null,duplicateOf:c.querySelector('.duplicate').value.trim()||null,developerNotes:c.querySelector('.notes').value};const r=await fetch('/v1/admin/submissions/'+c.dataset.id,{method:'PATCH',headers:{'content-type':'application/json'},body:JSON.stringify(body)});button.textContent=r.ok?'Saved':'Error';setTimeout(()=>{button.textContent='Save';button.disabled=false},1200)});
document.querySelector('#export').addEventListener('click',()=>{const ids=[...document.querySelectorAll('.card')].filter(c=>c.querySelector('.pick').checked).map(c=>c.dataset.id);if(ids.length)location.href='/v1/admin/export?'+ids.map(id=>'id='+encodeURIComponent(id)).join('&')});load();
</script></body></html>`;
}

function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, (character) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[character]!);
}
