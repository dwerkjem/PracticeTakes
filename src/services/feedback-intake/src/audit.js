const auditTable = document.querySelector("#audit");
const auditStatus = document.querySelector("#audit-status");

function escapeHtml(value) {
  const entities = {
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;",
  };
  return String(value ?? "").replace(/[&<>"']/g, (character) => entities[character]);
}

function actionTitle(action) {
  if (action.details?.title) return action.details.title;
  if (action.action === "create") return "Created feedback";
  if (action.action === "update") return "Updated feedback";
  return "Title unavailable";
}

async function loadAuditReceipts() {
  auditStatus.textContent = "Loading…";
  auditTable.innerHTML = "";

  try {
    const response = await fetch("/v1/admin/audit");
    const body = await response.json();
    if (!response.ok) throw new Error(body.error?.message || "Unable to load admin receipts.");

    const rows = body.actions.map((action) => `
      <tr>
        <td>${escapeHtml(new Date(action.createdAt).toLocaleString())}</td>
        <td>${escapeHtml(action.adminEmail)}</td>
        <td>${escapeHtml(action.action)}</td>
        <td class="receipt-title">${escapeHtml(actionTitle(action))}</td>
        <td><code>${escapeHtml(action.receiptId)}</code></td>
        <td><code>${escapeHtml(JSON.stringify(action.details))}</code></td>
      </tr>
    `).join("");

    auditTable.innerHTML = `
      <table>
        <thead>
          <tr>
            <th>Time</th>
            <th>Administrator</th>
            <th>Action</th>
            <th>Feedback title</th>
            <th>Receipt ID</th>
            <th>Details</th>
          </tr>
        </thead>
        <tbody>${rows}</tbody>
      </table>
    `;
    auditStatus.textContent = `${body.actions.length} action receipts`;
  } catch (error) {
    auditStatus.textContent = error.message;
    auditStatus.classList.add("error");
  }
}

document.querySelector("#refresh-audit").addEventListener("click", loadAuditReceipts);
loadAuditReceipts();
