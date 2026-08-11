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
  zoomed: null,
  // Previews default to large: the grid exists so you can see what is being
  // reviewed, and a thumbnail you have to squint at defeats the whole thing.
  size: window.localStorage.getItem("preview-size") || "large",
  // facet name -> Set of chosen values. Empty means "no opinion", which is what
  // makes several filters compose: within a facet the choices are alternatives,
  // between facets they all have to hold.
  filters: {},
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

  if (name === "history") loadHistory(element("history-machine").value);

  ["run", "review", "results", "history"].forEach((view) => {
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

async function stopRun() {
  const { ok, data } = await api("/api/stop-run", {});

  if (ok && data.stopping) {
    element("progress-message").textContent = "stopping after the current surface\u2026";
  }

  poll();
}

element("stop-run").addEventListener("click", stopRun);

// Escape as well as the button. The reason to want a key is the case where the
// pointer is not usable, which is what a capture run on the desktop display did
// until it moved to a display of its own -- and that is exactly when being able
// to stop matters most.
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && state.data && state.data.job && state.data.job.running) {
    stopRun();
  }
});

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

  // Only offered while there is something to stop, and disabled once asked so
  // a second press does not read as the first having been ignored.
  const stop = element("stop-run");
  stop.hidden = !job.running;
  stop.disabled = Boolean(job.stopping);
  stop.textContent = job.stopping ? "Stopping\u2026" : "Stop run";

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
    if (capture.notice) {
      // Not a failure: a hint to look, with the image still there to look at.
      const note = document.createElement("div");
      note.className = "capture-notice";
      note.textContent = capture.notice;
      card.appendChild(note);
    }

    const image = document.createElement("img");
    image.src = state.size === "dense" ? `/thumbnail?id=${capture.id}` : `/image?id=${capture.id}`;
    image.loading = "lazy";
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

  // The answer to anything a screenshot cannot settle: open the real
  // application on this exact surface, palette, and size.
  const open = document.createElement("button");
  open.type = "button";
  open.className = "open-in-app";
  open.textContent = "open in app";
  open.title = `Launch ${capture.state} in ${capture.theme}, ${capture.geometry} size`;
  open.addEventListener("click", (event) => {
    event.stopPropagation();
    openInApp(capture);
  });
  card.appendChild(open);

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

  let hidden = 0;

  view.groups.forEach((group) => {
    const shown = group.captures.filter((capture) => {
      const keep = matchesFilters(capture);

      if (!keep) hidden += 1;

      return keep;
    });

    if (!shown.length) return;

    const section = document.createElement("section");
    section.className = "surface-group";
    section.innerHTML = `<h2>${group.surface} <span class="state">${group.state}</span></h2>`;

    const row = document.createElement("div");
    row.className = `captures ${state.size}`;

    shown.forEach((capture) => {
      state.order.push(capture.id);
      row.appendChild(renderCard(capture));
    });

    section.appendChild(row);
    grid.appendChild(section);
  });

  if (!state.order.length) {
    grid.innerHTML = '<p class="muted">Nothing matches these filters.</p>';
  }

  // Said out loud, because a filtered grid that looks complete is how a
  // reviewer approves a run they only saw half of. Set, never appended.
  const filtered = element("filter-summary");

  if (filtered && hidden) {
    filtered.textContent = `${state.order.length} shown, ${hidden} hidden by the filters above.`;
  }

  paintSelection();
}


// --- Filters ---------------------------------------------------------------
//
// A full run is around a hundred captures. Reviewing that in one pass is only
// possible if you can narrow it to a question worth answering -- every settings
// window, everything in the light palette, everything with three tools open --
// and approve that set together.
//
// Within a facet, choosing two values means "either" (a capture is one theme or
// the other, so requiring both would match nothing). Between facets, every one
// has to hold. That is the combination people expect without being told.

// A filter is two sets: what to keep and what to drop. Include narrows to the
// values you name; exclude removes them and leaves everything else. Both are
// needed for different questions -- "only the settings window" and "everything
// except what I already approved" are not the same request, and the second one
// is what makes a second pass over a hundred captures finishable.

function filterFor(name) {
  return state.filters[name] || { include: new Set(), exclude: new Set() };
}

function activeFilters() {
  return Object.entries(state.filters)
    .filter(([, sets]) => sets.include.size || sets.exclude.size);
}

function valuesOf(capture, name) {
  const actual = capture.facets ? capture.facets[name] : undefined;

  if (actual === undefined || actual === null) return [];

  return Array.isArray(actual) ? actual : [actual];
}

function matchesFilters(capture) {
  return Object.entries(state.filters).every(([name, sets]) => {
    const values = valuesOf(capture, name);

    // Excluded wins over included: naming a value in both is a contradiction,
    // and dropping it is the safer reading of it.
    if (values.some((value) => sets.exclude.has(value))) return false;

    if (!sets.include.size) return true;

    return values.some((value) => sets.include.has(value));
  });
}

// Everything that depends on the filters is redrawn from the filters, rather
// than patched. Patching is what left "6 shown, 32 hidden" appended three times
// over and a cleared filter still described in the summary.
function applyFilters() {
  renderFilters(state.data);
  renderGrid(state.data);
}

// Click cycles a value: off -> include -> exclude -> off. Three states in one
// control, because a separate button per direction doubles the row and still
// has to say which one is on.
function cycleFilter(name, value) {
  const sets = filterFor(name);
  const include = new Set(sets.include);
  const exclude = new Set(sets.exclude);

  if (include.has(value)) {
    include.delete(value);
    exclude.add(value);
  } else if (exclude.has(value)) {
    exclude.delete(value);
  } else {
    include.add(value);
  }

  if (include.size || exclude.size) state.filters[name] = { include, exclude };
  else delete state.filters[name];   // Empty sets are no filter, not a filter of nothing.

  applyFilters();
}

function clearValue(name, value) {
  const sets = filterFor(name);
  const include = new Set(sets.include);
  const exclude = new Set(sets.exclude);
  include.delete(value);
  exclude.delete(value);

  if (include.size || exclude.size) state.filters[name] = { include, exclude };
  else delete state.filters[name];

  applyFilters();
}

function facetLabel(name) {
  return name.replace(/_/g, " ").replace(/^./, (first) => first.toUpperCase());
}

function facetMenu(name, values) {
  const menu = document.createElement("details");
  menu.className = "facet-menu";

  const sets = filterFor(name);
  const chosenCount = sets.include.size + sets.exclude.size;
  const summary = document.createElement("summary");
  summary.innerHTML = `${facetLabel(name)}` +
    (chosenCount ? ` <span class="badge">${chosenCount}</span>` : "") +
    ` <span class="caret">▾</span>`;
  menu.appendChild(summary);

  const panel = document.createElement("div");
  panel.className = "facet-panel";
  panel.innerHTML = '<div class="facet-hint">click to keep · again to exclude · again to clear</div>';

  values.forEach(([value, count]) => {
    const state_ = sets.include.has(value) ? "include"
      : sets.exclude.has(value) ? "exclude" : "off";
    const row = document.createElement("button");
    row.type = "button";
    row.className = `facet-option ${state_}`;
    row.innerHTML =
      `<span class="mark">${state_ === "include" ? "✓" : state_ === "exclude" ? "✕" : ""}</span>` +
      `<span class="value">${value}</span><span class="count">${count}</span>`;
    row.addEventListener("click", () => cycleFilter(name, value));
    panel.appendChild(row);
  });

  menu.appendChild(panel);

  // One open at a time, so a row of dropdowns cannot cover the grid.
  menu.addEventListener("toggle", () => {
    if (!menu.open) return;

    document.querySelectorAll("details.facet-menu").forEach((other) => {
      if (other !== menu) other.open = false;
    });
  });

  return menu;
}

function renderFilters(view) {
  const holder = element("filters");
  const facets = view.facets || {};

  holder.innerHTML = "";

  if (!Object.keys(facets).length) return;

  const row = document.createElement("div");
  row.className = "filter-row";

  Object.entries(facets).forEach(([name, values]) => {
    if (values.length < 2) return;   // A facet with one value filters nothing.

    row.appendChild(facetMenu(name, values));
  });

  holder.appendChild(row);

  // What is chosen shows outside the menus, because a filter you cannot see is
  // a grid lying about how much of the run you have looked at.
  const active = document.createElement("div");
  active.className = "active-filters";
  const picked = activeFilters();

  picked.forEach(([name, sets]) => {
    const chip = (value, excluded) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = `chip-button ${excluded ? "excluded" : "on"}`;
      button.innerHTML = `${excluded ? "not " : ""}${facetLabel(name)}: ${value}` +
        ` <span class="remove">✕</span>`;
      button.title = "Remove this filter";
      button.addEventListener("click", () => clearValue(name, value));
      active.appendChild(button);
    };

    sets.include.forEach((value) => chip(value, false));
    sets.exclude.forEach((value) => chip(value, true));
  });

  if (picked.length) {
    const clear = document.createElement("button");
    clear.type = "button";
    clear.className = "chip-button";
    clear.textContent = "clear all";
    clear.addEventListener("click", () => {
      state.filters = {};
      applyFilters();
    });
    active.appendChild(clear);
  }

  holder.appendChild(active);

  const summary = document.createElement("div");
  summary.id = "filter-summary";
  summary.textContent = picked.length
    ? ""   // Filled in by the grid, which is what knows the counts.
    : "Showing everything. Open a menu to keep or exclude values; filters combine.";
  holder.appendChild(summary);
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

// --- Bulk actions ----------------------------------------------------------

// Most surfaces are untouched by most changes: you look at a row of them,
// nothing is wrong, and answering three axes each individually is friction for
// no information.
async function scoreSelection(verdict) {
  if (!state.selected.size) {
    window.alert("Select some images first — click, shift-click for a range, or drag across them.");
    return;
  }

  let note = "";

  if (verdict === "fail") {
    note = window.prompt(
      `What is wrong with these ${state.selected.size} image(s)? (optional)`) || "";
  }

  const { ok, data } = await api("/api/score-many", {
    capture_ids: [...state.selected],
    verdict,
    note,
    overwrite: element("overwrite").checked,
  });

  if (!ok) {
    window.alert((data.problems || [data.error || "could not record that"]).join("\n"));
  } else if (data.left_alone) {
    // Said out loud, because silently skipping answered questions would look
    // like the button did nothing.
    element("outstanding").textContent =
      `Scored ${data.scored}; left ${data.left_alone} already-answered question(s) alone ` +
      "(tick “overwrite answered” to replace them).";
  }

  await reload();
}

element("approve-shown").addEventListener("click", async () => {
  if (!state.order.length) {
    window.alert("Nothing is shown to approve.");
    return;
  }

  const filters = Object.entries(state.filters).filter(([, values]) => values.size);
  const describe = filters.length
    ? filters.map(([name, values]) => `${name}: ${[...values].join(" or ")}`).join(", ")
    : "the whole run";

  if (!window.confirm(
    `Approve ${state.order.length} capture(s) — ${describe}?\n\n` +
    "Every question they can be asked from the image is answered pass. " +
    "Anything already answered is left alone.")) return;

  const { ok, data } = await api("/api/score-many", {
    capture_ids: state.order,
    verdict: "pass",
  });

  if (!ok) window.alert((data.problems || ["could not record that"]).join("\n"));

  await reload();
});

element("bulk-pass").addEventListener("click", () => scoreSelection("pass"));
element("bulk-fail").addEventListener("click", () => scoreSelection("fail"));
element("bulk-skip").addEventListener("click", () => scoreSelection("skip"));

element("select-all").addEventListener("click", () => {
  // Everything *shown*, not everything in the run: with filters on, selecting
  // what is hidden is never what was meant.
  state.order.forEach((id) => state.selected.add(id));
  paintSelection();
});

element("select-none").addEventListener("click", () => {
  state.selected.clear();
  paintSelection();
});

function applySize(size) {
  state.size = size;
  window.localStorage.setItem("preview-size", size);
  document.querySelectorAll("button.size").forEach((button) => {
    button.classList.toggle("active", button.dataset.size === size);
  });
  reload();
}

document.querySelectorAll("button.size").forEach((button) => {
  button.addEventListener("click", () => applySize(button.dataset.size));
});

// --- Opening the application -----------------------------------------------

async function openInApp(capture) {
  const banner = element("launch-banner");
  banner.hidden = false;
  banner.textContent = `Opening ${capture.surface} …`;

  const { ok, data } = await api("/api/launch", { capture_id: capture.id });

  if (!ok) {
    banner.textContent = data.error || "could not open it";
    banner.className = "launch failed";
    return;
  }

  banner.className = "launch";
  renderLaunch(data, capture.surface);
}

function renderLaunch(status, surfaceName) {
  const banner = element("launch-banner");

  if (!status || !status.running) {
    banner.hidden = true;
    return;
  }

  banner.hidden = false;
  banner.className = "launch";
  banner.innerHTML = "";
  banner.append(
    `Open in the application: ${surfaceName || status.state} · ${status.theme} · ${status.geometry} `);

  const close = document.createElement("button");
  close.type = "button";
  close.textContent = "close it";
  close.addEventListener("click", async () => {
    await api("/api/launch/close", {});
    renderLaunch({ running: false });
  });
  banner.appendChild(close);
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
  // A reason is asked for on a failure and is not required: cancel or leave it
  // empty and the failure is still recorded. Blocking here only interrupted
  // somebody who could already see what was wrong.
  let note = question.note || "";

  if (verdict === "fail") {
    note = window.prompt(
      `What is wrong with "${question.prompt}"? (optional)`, note) || "";
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

function capturesInOrder() {
  const shown = [];

  (state.data.groups || []).forEach((group) => {
    group.captures.forEach((capture) => {
      if (matchesFilters(capture)) shown.push(capture);
    });
  });

  return shown;
}

// Zoom is where judging happens: the image is finally big enough to judge, so
// the verdict lives here rather than back in the grid, and ← → walk the
// filtered set without closing.
function zoom(capture) {
  state.zoomed = capture;

  const overlay = element("zoom");
  const image = element("zoom-image");
  const answered = capture.questions.filter((question) => question.verdict).length;

  element("zoom-caption").textContent =
    `${capture.surface} · ${capture.geometry} · ${capture.theme} · ` +
    `${capture.width}×${capture.height}` +
    (capture.tags.length ? `  ·  ${capture.tags.join(", ")}` : "") +
    `  ·  ${answered} of ${capture.questions.length} answered`;

  element("zoom-verdicts").innerHTML = capture.questions.map((question) =>
    `<span>${question.prompt} ${verdictMarkup(question)}</span>`).join("");

  image.src = `/image?id=${capture.id}`;
  image.alt = `${capture.surface} at ${capture.geometry}`;
  image.classList.remove("actual-size");
  overlay.hidden = false;
}

function closeZoom() {
  element("zoom").hidden = true;
  state.zoomed = null;
}

function stepZoom(direction) {
  if (!state.zoomed) return;

  const shown = capturesInOrder();
  const at = shown.findIndex((capture) => capture.id === state.zoomed.id);

  if (at === -1) return;

  const next = shown[at + direction];

  if (next) zoom(next);
}

// Approving from zoom answers every question the image can answer, then moves
// on -- which is the rhythm the whole grid is for.
async function judgeZoomed(verdict) {
  if (!state.zoomed) return;

  let note = "";

  if (verdict === "fail") {
    note = window.prompt(
      `What is wrong with "${state.zoomed.surface}" at ${state.zoomed.geometry}? (optional)`) || "";
  }

  const { ok, data } = await api("/api/score-many", {
    capture_ids: [state.zoomed.id],
    verdict,
    note,
    overwrite: true,
  });

  if (!ok) {
    window.alert((data.problems || [data.error || "could not record that"]).join("\n"));
    return;
  }

  const shown = capturesInOrder();
  const at = shown.findIndex((capture) => capture.id === state.zoomed.id);
  const next = shown[at + 1];

  await reload();

  // reload() rebuilt the view, so the next capture has to be found again.
  const refreshed = capturesInOrder().find((capture) => next && capture.id === next.id);

  if (refreshed) zoom(refreshed);
  else closeZoom();
}

element("zoom-approve").addEventListener("click", () => judgeZoomed("pass"));
element("zoom-reject").addEventListener("click", () => judgeZoomed("fail"));
element("zoom-prev").addEventListener("click", () => stepZoom(-1));
element("zoom-next").addEventListener("click", () => stepZoom(1));
element("zoom-close").addEventListener("click", closeZoom);
element("zoom-open").addEventListener("click", () => {
  if (state.zoomed) openInApp(state.zoomed);
});

// Clicking the image toggles fit-to-window and actual size; clicking the
// backdrop closes. Clicking the bar does neither.
element("zoom-image").addEventListener("click", (event) => {
  event.stopPropagation();
  event.currentTarget.classList.toggle("actual-size");
});

element("zoom").addEventListener("click", (event) => {
  if (event.target.id === "zoom") closeZoom();
});

window.addEventListener("keydown", (event) => {
  if (element("zoom").hidden) return;

  if (event.key === "Escape") closeZoom();
  else if (event.key === "ArrowLeft") stepZoom(-1);
  else if (event.key === "ArrowRight") stepZoom(1);
  else if (event.key === "a" || event.key === "A") judgeZoomed("pass");
  else if (event.key === "f" || event.key === "F") judgeZoomed("fail");
  else return;

  event.preventDefault();
});

// --- History ---------------------------------------------------------------
//
// Chart.js (vendored in web/vendor/, MIT) draws these. Two rules shape what it
// is asked to draw, both from getting them wrong being worse than not drawing
// them at all:
//
//   * **One axis per chart.** Launch time in ms and a FIFO push in ns never
//     share a y-scale, so performance is small multiples — one metric, one
//     chart, its own scale, its own unit — rather than one crowded plot.
//   * **One machine per view.** A timing from another processor is not a point
//     on this machine's line.
//
// Colours: one series hue for the line, and the critical status colour for runs
// that had failures. Those two were checked against this surface for contrast
// and for colour-vision separation; a failing run also gets a larger ringed
// point and says so in the caption, so colour never carries it alone.

const INK = {
  series: "#3987e5",
  bad: "#d03b3b",
  grid: "#2c313c",
  text: "#9aa3b2",
  surface: "#191c22",
};

const charts = new Map();

function baseOptions({ unit = "", suggestedMin, suggestedMax, yTitle = "" } = {}) {
  return {
    responsive: true,
    maintainAspectRatio: false,
    interaction: { mode: "index", intersect: false },
    plugins: {
      // One series per chart, so the title names it and a legend box would be
      // a label for something already labelled.
      legend: { display: false },
      tooltip: {
        backgroundColor: "#12141a",
        borderColor: INK.grid,
        borderWidth: 1,
        titleColor: "#e6e8ee",
        bodyColor: "#e6e8ee",
        callbacks: {
          label: (item) => ` ${formatValue(item.parsed.y)}${unit ? " " + unit : ""}`,
        },
      },
    },
    scales: {
      x: {
        grid: { color: INK.grid, drawTicks: false },
        border: { color: INK.grid },
        ticks: { color: INK.text, maxRotation: 0, autoSkipPadding: 24, font: { size: 11 } },
      },
      y: {
        suggestedMin,
        suggestedMax,
        title: yTitle ? { display: true, text: yTitle, color: INK.text } : undefined,
        grid: { color: INK.grid, drawTicks: false },
        border: { display: false },
        ticks: { color: INK.text, font: { size: 11 } },
      },
    },
  };
}

function drawChart(canvas, config) {
  const existing = charts.get(canvas.id);

  if (existing) existing.destroy();

  charts.set(canvas.id, new window.Chart(canvas, config));
}

function formatValue(value) {
  if (value === null || value === undefined) return "—";
  if (Math.abs(value) >= 1000) return value.toFixed(0);
  if (Math.abs(value) >= 10) return value.toFixed(1);

  return Number(value.toFixed(3)).toString();
}

function shortDate(iso) {
  return iso.slice(5, 16).replace("T", " ");
}

function statTile(label, value, detail, tone = "") {
  return `<div class="tile">
    <div class="tile-label">${label}</div>
    <div class="tile-value ${tone}">${value}</div>
    <div class="tile-detail muted small">${detail || ""}</div>
  </div>`;
}

function renderQualityChart(runs) {
  const holder = element("chart-quality");

  if (!runs.length) {
    holder.innerHTML = '<p class="muted">No scored runs yet.</p>';
    return;
  }

  holder.innerHTML = `<div class="plot"><canvas id="canvas-quality"></canvas></div>
    <p class="muted small">Percentage of answered questions that passed. Skips count against it —
    an area nobody examined is not a pass. Larger red points are runs that had failures.</p>`;

  const lowest = Math.min(...runs.map((run) => run.pass_percent));

  drawChart(element("canvas-quality"), {
    type: "line",
    data: {
      labels: runs.map((run) => `${shortDate(run.started_at)}  ${run.commit.slice(0, 7)}`),
      datasets: [{
        data: runs.map((run) => run.pass_percent),
        borderColor: INK.series,
        backgroundColor: "rgba(57, 135, 229, 0.12)",
        borderWidth: 2,
        fill: true,
        tension: 0.15,
        pointRadius: runs.map((run) => (run.failed || run.capture_failures ? 6 : 4)),
        pointBackgroundColor: runs.map((run) =>
          (run.failed || run.capture_failures ? INK.bad : INK.series)),
        pointBorderColor: INK.surface,
        pointBorderWidth: 2,
      }],
    },
    // Zoomed to the data rather than pinned to zero: the interesting band for a
    // pass rate is the top few points, and a 0–100 axis draws every run as the
    // same flat line at the ceiling. Capped at 100 so the scale cannot imply
    // more than everything.
    options: baseOptions({
      unit: "%",
      suggestedMin: Math.max(0, Math.floor(lowest - 3)),
      suggestedMax: 100,
      yTitle: "% passing",
    }),
  });
}

function renderMetricCharts(metrics) {
  const holder = element("chart-metrics");

  if (!metrics.length) {
    holder.innerHTML = '<p class="muted">No measurements yet — run the Benchmarks suite.</p>';
    return;
  }

  holder.innerHTML = metrics.map((metric, index) => {
    const points = metric.points;
    const last = points[points.length - 1].value;
    const first = points[0].value;
    const change = points.length > 1 && first
      ? `${last > first ? "+" : ""}${(((last - first) / first) * 100).toFixed(1)}% since the first run`
      : "one run so far";

    return `<figure class="multiple">
      <figcaption>${metric.metric}
        <span class="muted small">${formatValue(last)} ${metric.unit} · ${change}</span>
      </figcaption>
      <div class="plot small"><canvas id="canvas-metric-${index}"></canvas></div>
    </figure>`;
  }).join("");

  metrics.forEach((metric, index) => {
    drawChart(element(`canvas-metric-${index}`), {
      type: "line",
      data: {
        labels: metric.points.map((point) =>
          `${shortDate(point.started_at)}  ${point.commit.slice(0, 7)}`),
        datasets: [{
          data: metric.points.map((point) => point.value),
          borderColor: INK.series,
          backgroundColor: "rgba(57, 135, 229, 0.12)",
          borderWidth: 2,
          fill: true,
          tension: 0.15,
          pointRadius: 4,
          pointBackgroundColor: INK.series,
          pointBorderColor: INK.surface,
          pointBorderWidth: 2,
        }],
      },
      options: baseOptions({ unit: metric.unit, yTitle: metric.unit }),
    });
  });
}

function renderHistory(data) {
  state.history = data;

  const machines = element("history-machine");
  machines.innerHTML = (data.machines || []).map((entry) =>
    `<option value="${entry.identity}" ${entry.identity === data.machine ? "selected" : ""}>` +
    `${entry.description || entry.identity.slice(0, 8)}</option>`).join("");

  element("history-summary").textContent =
    `${data.runs.length} scored run(s) · ${data.metrics.length} metric(s) tracked · ` +
    `${data.synced} synced, ${data.local_only} on this machine only`;

  const runs = data.runs;
  const latest = runs.length ? runs[runs.length - 1] : null;
  const previous = runs.length > 1 ? runs[runs.length - 2] : null;
  const delta = latest && previous
    ? (latest.pass_percent - previous.pass_percent).toFixed(1) : null;

  element("history-tiles").innerHTML = [
    statTile("Passing, latest run",
      latest ? `${latest.pass_percent}%` : "—",
      latest ? `${latest.passed} passed · ${latest.failed} failed · ${latest.skipped} skipped`
             : "nothing scored yet",
      latest && latest.failed ? "bad" : ""),
    statTile("Change from the run before",
      delta === null ? "—" : `${delta > 0 ? "+" : ""}${delta} pts`,
      previous ? `against ${previous.commit.slice(0, 8)}` : "no earlier run"),
    statTile("Runs recorded", String(runs.length),
      data.machines.length > 1 ? `${data.machines.length} machines known` : "this machine"),
  ].join("");

  renderQualityChart(runs);
  renderMetricCharts(data.metrics);

  element("table-runs").innerHTML = "<h3>Runs</h3>" + table(runs, [
    { key: "started_at", label: "Started" },
    { label: "Commit", render: (row) => row.commit.slice(0, 8) },
    { key: "mode", label: "Mode" },
    { label: "Passing", render: (row) => `${row.pass_percent}%` },
    { key: "passed", label: "Passed" },
    { key: "failed", label: "Failed" },
    { key: "skipped", label: "Skipped" },
  ]);

  element("table-metrics").innerHTML = "<h3>Measurements</h3>" + table(
    data.metrics.map((metric) => ({
      metric: metric.metric,
      unit: metric.unit,
      latest: formatValue(metric.points[metric.points.length - 1].value),
      points: metric.points.length,
    })), [
      { key: "metric", label: "Metric" },
      { key: "latest", label: "Latest" },
      { key: "unit", label: "Unit" },
      { key: "points", label: "Runs" },
    ]);
}

async function loadHistory(machine) {
  const { data } = await api(`/api/history${machine ? `?machine=${encodeURIComponent(machine)}` : ""}`);
  renderHistory(data);
}

element("history-machine").addEventListener("change", (event) => loadHistory(event.target.value));

element("sync-history").addEventListener("click", async () => {
  const { data } = await api("/api/sync", {});
  window.alert(
    `${data.written.length} run(s) written to ${data.directory}\n` +
    `${data.total} run(s) in the history directory.\n\n` +
    "Commit that directory to share this history. Images stay on this machine.");
  await loadHistory(element("history-machine").value);
});

element("toggle-tables").addEventListener("click", () => {
  const tables = element("history-tables");
  tables.hidden = !tables.hidden;
  element("toggle-tables").textContent = tables.hidden ? "Show as tables" : "Hide tables";
});

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

async function reload() {
  const scroll = window.scrollY;
  const { data } = await api("/api/run");
  state.data = data;

  renderBuilds(data);
  renderOptions(data);
  renderSuites(data);
  renderSummary(data);
  renderResults(data);
  renderFilters(data);
  renderGrid(data);
  renderLaunch(data.launch);
  renderJob(data.job);
  document.querySelectorAll("button.size").forEach((button) => {
    button.classList.toggle("active", button.dataset.size === state.size);
  });

  if (data.job && data.job.running) poll();

  window.scrollTo(0, scroll);
}

// The toolbar sticks below the header, so its offset has to follow the header's
// real height -- which changes when it wraps on a narrow window.
function trackHeaderHeight() {
  const header = document.querySelector("header");

  const apply = () => document.documentElement.style.setProperty(
    "--header-height", `${header.offsetHeight}px`);

  apply();
  window.addEventListener("resize", apply);

  if (window.ResizeObserver) new window.ResizeObserver(apply).observe(header);
}

trackHeaderHeight();
bandSelect();
reload();
