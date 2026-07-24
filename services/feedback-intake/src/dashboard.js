const results = document.querySelector("#results");
const resultCount = document.querySelector("#count");
const filterForm = document.querySelector("#filters");
const createForm = document.querySelector("#create");
const operationsSummary = document.querySelector("#operations-summary");

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

async function requestJson(url, options) {
  const response = await fetch(url, options);
  const responseText = await response.text();
  let body = {};

  try {
    body = responseText ? JSON.parse(responseText) : {};
  } catch {
    body = {
      error: {
        message: responseText || "The server returned an unexpected response.",
      },
    };
  }

  if (!response.ok) {
    throw new Error(body.error?.message || "The request failed.");
  }

  return body;
}

function selectOptions(values, selectedValue) {
  return values.map((value) => {
    const selected = selectedValue === value ? "selected" : "";
    const label = value.replace("_", " ");
    return `<option ${selected} value="${value}">${label}</option>`;
  }).join("");
}

function feedbackCard(feedback) {
  const categories = ["bug", "idea", "usability", "other"];
  const statuses = ["new", "needs_review", "planned", "duplicate", "resolved", "declined"];
  const priorities = ["low", "medium", "high", "critical"];
  const tags = feedback.tags ?? [];

  return `
    <article class="card" data-id="${escapeHtml(feedback.receiptId)}">
      <div class="meta">
        <input class="pick" type="checkbox" aria-label="Select">
        <strong>${escapeHtml(feedback.category)}</strong>
        <span>${escapeHtml(feedback.status)}</span>
        <span>${escapeHtml(feedback.appVersion)}</span>
        <time>${escapeHtml(new Date(feedback.receivedAt).toLocaleString())}</time>
        <code>${escapeHtml(feedback.receiptId)}</code>
      </div>

      <div class="editor">
        <div class="controls">
          <label>Category<select class="category">${selectOptions(categories, feedback.category)}</select></label>
          <label>Version<input class="version" value="${escapeHtml(feedback.appVersion)}"></label>
          <label>Email<input class="email" type="email" value="${escapeHtml(feedback.contactEmail)}" placeholder="Not provided"></label>
          <label>Status<select class="status">${selectOptions(statuses, feedback.status)}</select></label>
          <label>Priority<select class="priority"><option value="">No priority</option>${selectOptions(priorities, feedback.priority)}</select></label>
          <label>Tags<input class="tags" value="${escapeHtml(tags.join(", "))}" placeholder="Comma separated"></label>
          <label>GitHub issue<input class="github" value="${escapeHtml(feedback.githubIssueUrl)}" placeholder="Issue URL"></label>
          <label>Duplicate<input class="duplicate" value="${escapeHtml(feedback.duplicateOf)}" placeholder="Receipt ID"></label>
          <label>Quarantine<input class="quarantine" value="${escapeHtml(feedback.quarantineReason)}" placeholder="Not quarantined"></label>
        </div>

        <div class="feedback-summary">
          <label>Type<input class="feedback-type" value="${escapeHtml(feedback.feedbackType)}" placeholder="Feedback type"></label>
          <label class="title-box">Title<input class="title" value="${escapeHtml(feedback.title)}" placeholder="Feedback title"></label>
          <label>Contact<input class="contact-summary" value="${escapeHtml(feedback.contactSummary)}" placeholder="Not provided"></label>
        </div>

        <label class="content-box">
          Feedback content
          <textarea class="feedback" placeholder="Feedback content">${escapeHtml(feedback.description)}</textarea>
        </label>

        <div class="secondary-content">
          <label>Feature context<textarea class="context-tag">${escapeHtml(feedback.contextTag)}</textarea></label>
          <label>Diagnostic context<textarea class="diagnostic">${escapeHtml(feedback.diagnosticContext)}</textarea></label>
          <label>Developer notes<textarea class="notes">${escapeHtml(feedback.developerNotes)}</textarea></label>
        </div>

        <div class="card-actions">
          ${feedback.hasScreenshot ? '<button class="view-screenshot" type="button">View screenshot</button>' : ""}
          <button class="save" type="button">Save changes</button>
          <button class="delete danger" type="button">Delete</button>
          <span class="card-status"></span>
        </div>
      </div>
    </article>
  `;
}

async function loadFeedback() {
  results.innerHTML = '<p class="empty">Loading…</p>';

  try {
    const query = new URLSearchParams(new FormData(filterForm));
    for (const [key, value] of [...query]) {
      if (!value) query.delete(key);
    }

    const body = await requestJson(`/v1/admin/submissions?${query}`);
    resultCount.textContent = `${body.submissions.length} submissions`;
    results.innerHTML = body.submissions.map(feedbackCard).join("") ||
      '<p class="empty">No matching feedback.</p>';
  } catch (error) {
    results.innerHTML = `<p class="empty error">${escapeHtml(error.message)}</p>`;
  }
}

async function viewScreenshot(card) {
  const response = await fetch(`/v1/admin/submissions/${card.dataset.id}/screenshot`);
  if (!response.ok) throw new Error("The screenshot could not be loaded.");
  const imageUrl = URL.createObjectURL(await response.blob());
  const dialog = document.createElement("dialog");
  dialog.className = "screenshot-viewer";
  dialog.innerHTML = `
    <div class="screenshot-toolbar">
      <strong>Feedback screenshot</strong>
      <button type="button">Close</button>
    </div>
    <img alt="Screenshot attached to feedback ${escapeHtml(card.dataset.id)}">
  `;
  dialog.querySelector("img").src = imageUrl;
  dialog.querySelector("button").addEventListener("click", () => dialog.close());
  dialog.addEventListener("close", () => {
    URL.revokeObjectURL(imageUrl);
    dialog.remove();
  });
  document.body.append(dialog);
  dialog.showModal();
}

function feedbackUpdateFromCard(card) {
  const feedbackType = card.querySelector(".feedback-type").value.trim();
  const title = card.querySelector(".title").value.trim();
  const description = card.querySelector(".feedback").value.trim();
  const contact = card.querySelector(".contact-summary").value.trim();
  const contextTag = card.querySelector(".context-tag").value.trim();
  const diagnosticContext = card.querySelector(".diagnostic").value.trim();
  const structuredMessage = feedbackType || title || contact
    ? `Type: ${feedbackType}\nTitle: ${title}\n\nDescription:\n${description}\n\nContact: ${contact}`
    : description;
  const message = diagnosticContext
    ? `${structuredMessage}\nEnvironment: ${diagnosticContext}`
    : structuredMessage;
  const messageWithContext = contextTag
    ? `${message}\n\nFeedback context (user-editable):\n${contextTag}`
    : message;

  return {
    category: card.querySelector(".category").value,
    appVersion: card.querySelector(".version").value.trim(),
    contactEmail: card.querySelector(".email").value.trim() || null,
    message: messageWithContext,
    status: card.querySelector(".status").value,
    priority: card.querySelector(".priority").value || null,
    tags: card.querySelector(".tags").value.split(",").map((tag) => tag.trim()).filter(Boolean),
    githubIssueUrl: card.querySelector(".github").value.trim() || null,
    duplicateOf: card.querySelector(".duplicate").value.trim() || null,
    quarantineReason: card.querySelector(".quarantine").value.trim() || null,
    developerNotes: card.querySelector(".notes").value,
  };
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
}

async function loadOperations() {
  try {
    const { operations } = await requestJson("/v1/admin/operations");
    const availability = operations.availabilityPercent === null
      ? "No requests recorded"
      : `${operations.availabilityPercent.toFixed(2)}% availability`;
    operationsSummary.textContent =
      `${availability}; ${operations.failures} failures; ` +
      `${operations.averageResponseMs.toFixed(0)} ms average response; ` +
      `${operations.storedSubmissions} stored (${formatBytes(operations.estimatedStoredBytes)}); ` +
      `${operations.quarantinedSubmissions} quarantined; ` +
      `last retention ${operations.lastRetentionAt
        ? new Date(operations.lastRetentionAt).toLocaleString()
        : "not yet recorded"}`;
  } catch (error) {
    operationsSummary.textContent = error.message;
  }
}

async function deleteFeedback(card) {
  if (!window.confirm("Permanently delete this feedback record?")) return;

  const status = card.querySelector(".card-status");
  try {
    await requestJson(`/v1/admin/submissions/${card.dataset.id}`, { method: "DELETE" });
    card.remove();
    resultCount.textContent = `${document.querySelectorAll(".card").length} submissions`;
  } catch (error) {
    status.textContent = error.message;
  }
}

async function saveFeedback(card, button) {
  const status = card.querySelector(".card-status");
  button.disabled = true;

  try {
    await requestJson(`/v1/admin/submissions/${card.dataset.id}`, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(feedbackUpdateFromCard(card)),
    });
    status.textContent = "Saved";
    return true;
  } catch (error) {
    status.textContent = error.message;
    return false;
  } finally {
    button.disabled = false;
  }
}

filterForm.addEventListener("submit", (event) => {
  event.preventDefault();
  loadFeedback();
});

results.addEventListener("click", async (event) => {
  const card = event.target.closest(".card");
  if (!card) return;

  if (event.target.classList.contains("delete")) {
    await deleteFeedback(card);
  } else if (event.target.classList.contains("save")) {
    await saveFeedback(card, event.target);
  } else if (event.target.classList.contains("view-screenshot")) {
    try {
      await viewScreenshot(card);
    } catch (error) {
      card.querySelector(".card-status").textContent = error.message;
    }
  }
});

createForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const status = document.querySelector("#create-status");
  const feedback = Object.fromEntries(new FormData(createForm));

  if (feedback.submittedAt) {
    feedback.submittedAt = new Date(feedback.submittedAt).toISOString();
  } else {
    delete feedback.submittedAt;
  }
  if (!feedback.contactEmail) delete feedback.contactEmail;
  if (!feedback.diagnosticContext) delete feedback.diagnosticContext;

  try {
    await requestJson("/v1/admin/submissions", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(feedback),
    });
    createForm.reset();
    status.textContent = "Created";
    await loadFeedback();
  } catch (error) {
    status.textContent = error.message;
  }
});

function selectedCards() {
  return [...document.querySelectorAll(".card")]
    .filter((card) => card.querySelector(".pick").checked);
}

document.querySelector("#save-all").addEventListener("click", async (event) => {
  const cards = [...document.querySelectorAll(".card")];
  const bulkStatus = document.querySelector("#bulk-status");
  event.target.disabled = true;
  bulkStatus.textContent = `Saving ${cards.length} records…`;

  let saved = 0;
  for (const card of cards) {
    const success = await saveFeedback(card, card.querySelector(".save"));
    if (success) saved += 1;
  }

  bulkStatus.textContent = saved === cards.length
    ? `Saved all ${saved} records`
    : `Saved ${saved} of ${cards.length} records`;
  event.target.disabled = false;
});

document.querySelector("#delete-selected").addEventListener("click", async (event) => {
  const cards = selectedCards();
  const bulkStatus = document.querySelector("#bulk-status");
  if (!cards.length) {
    bulkStatus.textContent = "Select at least one record";
    return;
  }
  if (!window.confirm(`Permanently delete ${cards.length} selected feedback records?`)) return;

  event.target.disabled = true;
  try {
    const receiptIds = cards.map((card) => card.dataset.id);
    const body = await requestJson("/v1/admin/submissions/batch-delete", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ receiptIds }),
    });
    const deleted = new Set(body.deleted);
    cards.filter((card) => deleted.has(card.dataset.id)).forEach((card) => card.remove());
    resultCount.textContent = `${document.querySelectorAll(".card").length} submissions`;
    bulkStatus.textContent = `Deleted ${body.deleted.length} records`;
  } catch (error) {
    bulkStatus.textContent = error.message;
  } finally {
    event.target.disabled = false;
  }
});

document.querySelector("#export").addEventListener("click", () => {
  const selectedIds = selectedCards().map((card) => card.dataset.id);

  if (selectedIds.length) {
    const query = selectedIds.map((id) => `id=${encodeURIComponent(id)}`).join("&");
    window.location.href = `/v1/admin/export?${query}`;
  }
});

document.querySelector("#run-retention").addEventListener("click", async (event) => {
  const status = document.querySelector("#retention-status");
  event.target.disabled = true;
  status.textContent = "Running…";
  try {
    const { retention } = await requestJson("/v1/admin/maintenance/retention", {
      method: "POST",
    });
    const deleted = retention.resolved + retention.duplicates + retention.declined;
    status.textContent = `Complete: ${deleted} feedback records expired`;
    await Promise.all([loadFeedback(), loadOperations()]);
  } catch (error) {
    status.textContent = error.message;
  } finally {
    event.target.disabled = false;
  }
});

loadFeedback();
loadOperations();
