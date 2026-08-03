// The thin part. Every decision — which suites exist, what a suite needs built,
// which questions a capture still owes, whether the run is finished — is
// answered by the server; this fetches, renders, and posts back.
//
// Two pieces of real behaviour live here because both are inherently pointer
// things: multi-select in the grid (click, shift-click, ctrl-click, drag), and
// polling a running job so the page says what is happening instead of freezing
// for the ten minutes a cold build takes.

const state = {
  view: "run",
  data: null,
  order: [],          // capture ids, in the order they are rendered
  selected: new Set(),
  lastClicked: null,
  polling: null,
};

async function api(path, body) {
  const response = await fetch(path, body
    ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }
    : undefined);

  return { ok: response.ok, data: await response.json().catch(() => ({})) };
}

const element = (id) => document.getElementById(id);

// --- Views -----------------------------------------------------------------

function showView(name) {
  state.view = name;
  ["run", "review", "results"].forEach((view) => {
    element(`view-${view}`).hidden = view !== name;
  });
  document.querySelectorAll("#tabs button").forEach((button) => {
    button.classList.toggle("active", button.dataset.view === name);
  });
}

document.querySelectorAll("#tabs button").forEach((button) => {
  button.addEventListener("click", () => showView(button.dataset.view));
});

// --- The run view ----------------------------------------------------------

function renderBuilds(view) {
  const holder = element("build-state");
  const missing = (view.builds || []).filter((build) => !build.present);
  const stale = (view.builds || []).filter((build) => build.present && build.stale);
  const lines = [];

  // Said up front rather than discovered ten minutes in: a cold build is the
  // slowest thing here by far, and knowing it is coming changes what you click.
  missing.forEach((build) => lines.push(
    `<div class="notice">${build.target} is not built — ${build.reason}. Running anything that needs it will build it first (several minutes).</div>`));
  stale.forEach((build) => lines.push(
    `<div class="notice subtle">${build.target} was built before your latest source change. Tick "rebuild" to be sure.</div>`));

  if (!view.display) {
    lines.push('<div class="notice">No display detected — UI suites will be skipped rather than failing.</div>');
  }

  holder.innerHTML = lines.join("");
}

function suiteRow(suite, result) {
  const verdict = result ? result.state : "";
  const detail = result && result.state && result.state !== "queued"
    ? `<span class="verdict-${verdict}">${verdict}${result.cases ? ` · ${result.cases} case(s)` : ""}` +
      `${result.failures ? ` · ${result.failures} failure(s)` : ""}` +
      `${result.seconds ? ` · ${result.seconds}s` : ""}</span>`
    : "";

  return `
    <label class="suite">
      <input type="checkbox" name="suite" value="${suite.id}" />
      <span class="suite-label">${suite.label}</span>
      <span class="muted small">${suite.description}</span>
      ${suite.needs_display ? '<span class="chip">needs a display</span>' : ""}
      ${suite.needs.length ? `<span class="chip">builds ${suite.needs.join(", ")}</span>` : ""}
      ${detail}
    </label>`;
}

function renderSuites(view) {
  const results = (view.job && view.job.results) || {};
  const holder = element("suite-groups");
  const titles = { tests: "Tests", performance: "Performance", ui: "User interface" };

  holder.innerHTML = view.kinds.map((kind) => `
    <section class="panel">
      <h2>${titles[kind] || kind}
        <button type="button" class="link" data-kind="${kind}">select all</button>
      </h2>
      ${view.suites.filter((suite) => suite.kind === kind)
        .map((suite) => suiteRow(suite, results[suite.id])).join("")}
    </section>`).join("");

  holder.querySelectorAll("button[data-kind]").forEach((button) => {
    button.addEventListener("click", () => {
      const wanted = view.suites.filter((suite) => suite.kind === button.dataset.kind)
        .map((suite) => suite.id);
      document.querySelectorAll('input[name="suite"]').forEach((box) => {
        if (wanted.includes(box.value)) box.checked = true;
      });
    });
  });
}

function renderOptions(view) {
  const mode = element("mode");

  if (!mode.options.length) {
    view.modes.forEach((name) => mode.add(new Option(name, name, name === "full", name === "full")));
    element("resolution-boxes").innerHTML = view.resolutions.map((name) =>
      `<label class="inline"><input type="checkbox" name="resolution" value="${name}" checked /> ${name}</label>`
    ).join("");
  }
}

function chosenSuites() {
  return [...document.querySelectorAll('input[name="suite"]:checked')].map((box) => box.value);
}

function chosenOptions() {
  return {
    mode: element("mode").value,
    resolutions: [...document.querySelectorAll('input[name="resolution"]:checked')].map((b) => b.value),
    rebuild: element("rebuild").checked,
  };
}

async function runSuites(ids) {
  if (!ids.length) {
    window.alert("Tick at least one suite, or use one of the run-everything buttons.");
    return;
  }

  const { ok, data } = await api("/api/run-suites", { suites: ids, ...chosenOptions() });

  if (!ok) {
    window.alert(data.error || "could not start");
    return;
  }

  showView("run");
  poll();
}

element("run-selected").addEventListener("click", () => runSuites(chosenSuites()));
element("run-all").addEventListener("click", () =>
  runSuites(state.data.suites.map((suite) => suite.id)));
["tests", "performance", "ui"].forEach((kind) => {
  element(`run-${kind === "tests" ? "tests" : kind}`).addEventListener("click", () =>
    runSuites(state.data.suites.filter((suite) => suite.kind === kind).map((suite) => suite.id)));
});

// --- Progress --------------------------------------------------------------

function renderJob(job) {
  const bar = element("progress");
  bar.hidden = !job || (!job.running && job.state === "idle");

  if (!job || job.state === "idle") return;

  element("progress-fill").style.width = `${job.percent}%`;
  element("progress-fill").className = job.state === "failed" ? "failed" : "";
  element("progress-message").textContent =
    `${job.state}${job.message ? ` — ${job.message}` : ""}`;

  if (job.log && job.log.length) element("log").textContent = job.log.join("\n");
  element("log").scrollTop = element("log").scrollHeight;
}

function poll() {
  if (state.polling) return;

  state.polling = window.setInterval(async () => {
    const { data } = await api("/api/job");
    renderJob(data);

    if (!data.running) {
      window.clearInterval(state.polling);
      state.polling = null;
      // Whatever it produced is what the page should now be showing.
      await reload();
    }
  }, 1000);
}

// --- Results ---------------------------------------------------------------

function table(rows, columns) {
  if (!rows.length) return '<p class="muted">Nothing recorded for this run yet.</p>';

  return `<table><thead><tr>${columns.map((c) => `<th>${c.label}</th>`).join("")}</tr></thead>
    <tbody>${rows.map((row) => `<tr>${columns.map((c) =>
      `<td>${c.render ? c.render(row) : row[c.key] ?? ""}</td>`).join("")}</tr>`).join("")}</tbody></table>`;
}

function renderResults(view) {
  element("results-table").innerHTML = table(view.results || [], [
    { key: "suite", label: "Suite" },
    { key: "cases", label: "Cases" },
    { label: "Failures", render: (row) =>
      row.failures ? `<span class="verdict-failed">${row.failures}</span>` : "0" },
    { label: "Seconds", render: (row) => Math.round(row.duration_seconds * 10) / 10 },
    { key: "recorded_at", label: "Recorded" },
  ]);

  element("measurements-table").innerHTML = table(view.measurements || [], [
    { key: "metric", label: "Metric" },
    { key: "value", label: "Value" },
    { key: "unit", label: "Unit" },
    { key: "scenario", label: "Scenario" },
  ]);
}

// --- The review grid -------------------------------------------------------

function verdictMarkup(question) {
  if (!question.verdict) return `<span class="muted">unanswered</span>`;

  const note = question.note ? ` — ${question.note}` : "";

  return `<span class="verdict-${question.verdict}">${question.verdict}${note}</span>`;
}

function renderCard(capture) {
  const card = document.createElement("div");
  card.className = "card";
  card.dataset.id = String(capture.id);

  const heading = document.createElement("h3");
  heading.textContent = `${capture.geometry} · ${capture.width}×${capture.height}`;
  card.appendChild(heading);

  if (capture.unavailable) {
    const box = document.createElement("div");
    box.className = "unavailable";
    box.textContent = capture.unavailable === "failed"
      ? `Capture failed — ${capture.failure}`
      : `Image ${capture.unavailable}`;
    card.appendChild(box);
  } else {
    const image = document.createElement("img");
    image.src = `/thumbnail?id=${capture.id}`;
    image.alt = `${capture.surface} at ${capture.geometry}`;
    image.addEventListener("click", (event) => {
      event.stopPropagation();
      zoom(capture);
    });
    card.appendChild(image);
  }

  const tags = document.createElement("div");
  tags.className = "tags";
  capture.tags.forEach((name) => {
    const chip = document.createElement("span");
    chip.className = "tag-chip";
    chip.textContent = name;
    tags.appendChild(chip);
  });
  card.appendChild(tags);

  const questions = document.createElement("div");
  questions.className = "questions";

  capture.questions.forEach((question) => {
    const row = document.createElement("div");
    row.className = `question${question.attended ? " attended" : ""}`;
    row.innerHTML = `<span class="prompt">${question.prompt}</span> ${verdictMarkup(question)}`;

    if (!question.attended) {
      ["pass", "fail", "skip"].forEach((verdict) => {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = verdict[0].toUpperCase();
        button.title = verdict;
        button.addEventListener("click", (event) => {
          event.stopPropagation();
          score(capture.id, question, verdict);
        });
        row.appendChild(button);
      });
    }

    questions.appendChild(row);
  });

  card.appendChild(questions);

  if (capture.comments.length) {
    const list = document.createElement("ul");
    list.className = "comments";
    capture.comments.forEach((body) => {
      const item = document.createElement("li");
      item.textContent = body;
      list.appendChild(item);
    });
    card.appendChild(list);
  }

  const comment = document.createElement("button");
  comment.type = "button";
  comment.textContent = "comment";
  comment.addEventListener("click", (event) => {
    event.stopPropagation();
    addComment(capture.id);
  });
  card.appendChild(comment);

  card.addEventListener("click", (event) => selectFrom(capture.id, event));

  return card;
}

function renderGrid(view) {
  state.order = [];

  const grid = element("grid");
  grid.innerHTML = "";

  if (view.empty || !view.groups) {
    grid.innerHTML = `<div class="empty">
      <p>Nothing has been captured yet.</p>
      <p class="muted">Run the <strong>UI capture</strong> suite from the Run tab —
      it builds the application if it has to, then photographs every surface.</p>
      <button type="button" id="capture-now" class="primary">Capture a run now</button>
    </div>`;
    const button = element("capture-now");

    if (button) button.addEventListener("click", () => runSuites(["ui-capture"]));

    return;
  }

  const outstanding = view.outstanding || [];
  const attended = outstanding.filter((entry) => entry.attended).length;
  element("outstanding").textContent = outstanding.length
    ? `${outstanding.length} unanswered (${attended} need the attended pass: \`test-suite attend\`). ` +
      "The run exports as incomplete until they are answered."
    : "Everything is scored.";

  element("tag-buttons").innerHTML = "";
  (view.tags || []).forEach((tag) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = tag.name;
    button.title = tag.description || "";
    button.addEventListener("click", (event) => applyTag(tag.name, event.shiftKey));
    element("tag-buttons").appendChild(button);
  });

  view.groups.forEach((group) => {
    const section = document.createElement("section");
    section.className = "surface-group";
    section.innerHTML = `<h2>${group.surface} <span class="state">${group.state}</span></h2>`;

    const row = document.createElement("div");
    row.className = "captures";

    group.captures.forEach((capture) => {
      state.order.push(capture.id);
      row.appendChild(renderCard(capture));
    });

    section.appendChild(row);
    grid.appendChild(section);
  });

  paintSelection();
}

// --- Selection -------------------------------------------------------------

function paintSelection() {
  document.querySelectorAll(".card").forEach((card) => {
    card.classList.toggle("selected", state.selected.has(Number(card.dataset.id)));
  });

  const count = state.selected.size;
  element("selection-count").textContent =
    count === 0 ? "nothing selected" : `${count} selected`;
}

function selectFrom(id, event) {
  if (event.shiftKey && state.lastClicked !== null) {
    const from = state.order.indexOf(state.lastClicked);
    const to = state.order.indexOf(id);
    const [start, end] = from < to ? [from, to] : [to, from];

    for (let index = start; index <= end; index += 1) state.selected.add(state.order[index]);
  } else if (event.ctrlKey || event.metaKey) {
    if (state.selected.has(id)) state.selected.delete(id);
    else state.selected.add(id);
  } else {
    state.selected.clear();
    state.selected.add(id);
  }

  state.lastClicked = id;
  paintSelection();
}

function bandSelect() {
  const band = element("rubber-band");
  let origin = null;

  element("grid").addEventListener("mousedown", (event) => {
    if (event.target.closest("button") || event.target.tagName === "IMG") return;

    origin = { x: event.clientX, y: event.clientY };
    band.hidden = false;
    band.style.left = `${origin.x}px`;
    band.style.top = `${origin.y}px`;
    band.style.width = "0px";
    band.style.height = "0px";
  });

  window.addEventListener("mousemove", (event) => {
    if (!origin) return;

    band.style.left = `${Math.min(origin.x, event.clientX)}px`;
    band.style.top = `${Math.min(origin.y, event.clientY)}px`;
    band.style.width = `${Math.abs(event.clientX - origin.x)}px`;
    band.style.height = `${Math.abs(event.clientY - origin.y)}px`;
  });

  window.addEventListener("mouseup", (event) => {
    if (!origin) return;

    const dragged = Math.abs(event.clientX - origin.x) > 4 || Math.abs(event.clientY - origin.y) > 4;
    band.hidden = true;

    if (!dragged) {
      origin = null;
      return;
    }

    const box = band.getBoundingClientRect();

    if (!event.shiftKey && !event.ctrlKey) state.selected.clear();

    document.querySelectorAll(".card").forEach((card) => {
      const bounds = card.getBoundingClientRect();
      const overlaps = !(bounds.right < box.left || bounds.left > box.right ||
                         bounds.bottom < box.top || bounds.top > box.bottom);

      if (overlaps) state.selected.add(Number(card.dataset.id));
    });

    origin = null;
    paintSelection();
  });
}

// --- Actions ---------------------------------------------------------------

async function applyTag(name, remove) {
  if (!state.selected.size) {
    window.alert("Select some images first.");
    return;
  }

  await api("/api/tag", { capture_ids: [...state.selected], tag: name, remove: Boolean(remove) });
  await reload();
}

async function score(captureId, question, verdict) {
  // A failure without a reason says something is wrong without saying what, so
  // the note is asked for here rather than rejected by the server.
  let note = question.note || "";

  if (verdict === "fail") {
    note = window.prompt(`What is wrong with "${question.prompt}"?`, note) || "";

    if (!note.trim()) return;
  }

  const { ok, data } = await api("/api/score", {
    capture_id: captureId,
    question: question.id,
    verdict,
    note,
  });

  if (!ok) window.alert((data.problems || ["could not record that"]).join("\n"));

  await reload();
}

async function addComment(captureId) {
  const body = window.prompt("Comment on this image:");

  if (!body || !body.trim()) return;

  await api("/api/comment", { capture_id: captureId, body });
  await reload();
}

function zoom(capture) {
  const overlay = element("zoom");
  const image = element("zoom-image");

  element("zoom-caption").textContent =
    `${capture.surface} · ${capture.geometry} · ${capture.width}×${capture.height}`;
  image.src = `/image?id=${capture.id}`;
  image.alt = `${capture.surface} at ${capture.geometry}`;
  overlay.hidden = false;
}

function closeZoom() {
  element("zoom").hidden = true;
}

// --- Wiring ----------------------------------------------------------------

function renderSummary(view) {
  const picker = element("run-picker");
  picker.innerHTML = (view.runs || []).map((run) =>
    `<option value="${run.id}" ${run.id === view.run_id ? "selected" : ""}>` +
    `run ${run.id} · ${run.mode} · ${run.started_at}</option>`).join("") ||
    '<option value="">no runs yet</option>';

  const summary = view.run
    ? `run ${view.run.id} · ${view.run.mode} · ${view.run.complete ? "complete" : "incomplete"} · ${view.run.commit}`
    : "no runs in the store";
  element("run-summary").textContent = summary;
}

element("run-picker").addEventListener("change", async (event) => {
  const value = event.target.value;

  if (!value) return;

  await api("/api/select", { run_id: Number(value) });
  await reload();
});

element("add-tag").addEventListener("click", async () => {
  const name = window.prompt("New tag:");

  if (!name || !name.trim()) return;

  await api("/api/tags", { name: name.trim() });
  await reload();
});

// Closing the zoom leaves scroll position and selection untouched, because it
// is an overlay over the same grid rather than a page of its own.
element("zoom").addEventListener("click", closeZoom);
window.addEventListener("keydown", (event) => {
  if (event.key === "Escape") closeZoom();
});

async function reload() {
  const scroll = window.scrollY;
  const { data } = await api("/api/run");
  state.data = data;

  renderBuilds(data);
  renderOptions(data);
  renderSuites(data);
  renderSummary(data);
  renderResults(data);
  renderGrid(data);
  renderJob(data.job);

  if (data.job && data.job.running) poll();

  window.scrollTo(0, scroll);
}

bandSelect();
reload();
