// The thin part. Every decision — which questions a capture owes, what a tag
// means, whether the run is finished — is answered by the server; this fetches
// and renders, and posts back what the reviewer did.
//
// The one piece of real behaviour here is selection, because multi-select is
// inherently a pointer thing: click, shift-click for a range, ctrl-click to
// toggle, drag a band across the grid. Everything a selection is then used for
// is one POST.

const state = {
  run: null,
  order: [],          // capture ids, in the order they are rendered
  selected: new Set(),
  lastClicked: null,
};

async function api(path, body) {
  const response = await fetch(path, body
    ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }
    : undefined);

  return { ok: response.ok, data: await response.json().catch(() => ({})) };
}

// --- Rendering -------------------------------------------------------------

function summarise(run, machine) {
  const status = run.complete ? "complete" : "incomplete";
  const resolutions = JSON.parse(run.resolutions || "[]").join(", ") || "default";

  document.getElementById("run-summary").innerHTML =
    `Run ${run.id} · ${run.mode} · ${status} · <code>${run.commit}</code>` +
    `<span class="muted"> — ${resolutions} — ${machine.processor}, ${machine.display}</span>`;
}

function renderOutstanding(outstanding) {
  const element = document.getElementById("outstanding");

  if (!outstanding.length) {
    element.textContent = "Everything is scored.";
    return;
  }

  const attended = outstanding.filter((entry) => entry.attended).length;
  element.textContent =
    `${outstanding.length} unanswered (${attended} need the attended pass: ` +
    `\`test-suite attend\`). The run exports as incomplete until they are answered.`;
}

function tagButtons(tags) {
  const holder = document.getElementById("tag-buttons");
  holder.innerHTML = "";

  tags.forEach((tag) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = tag.name;
    button.title = tag.description || "";
    button.addEventListener("click", (event) => applyTag(tag.name, event.shiftKey));
    holder.appendChild(button);
  });
}

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

function render(view) {
  state.run = view;
  state.order = [];

  summarise(view.run, view.machine);
  tagButtons(view.tags);
  renderOutstanding(view.outstanding);

  const grid = document.getElementById("grid");
  grid.innerHTML = "";

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

async function reload() {
  const { data } = await api("/api/run");
  const scroll = window.scrollY;
  render(data);
  window.scrollTo(0, scroll);
}

// --- Selection -------------------------------------------------------------

function paintSelection() {
  document.querySelectorAll(".card").forEach((card) => {
    card.classList.toggle("selected", state.selected.has(Number(card.dataset.id)));
  });

  const count = state.selected.size;
  document.getElementById("selection-count").textContent =
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
  const band = document.getElementById("rubber-band");
  let origin = null;

  document.getElementById("grid").addEventListener("mousedown", (event) => {
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
  const overlay = document.getElementById("zoom");
  const image = document.getElementById("zoom-image");

  document.getElementById("zoom-caption").textContent =
    `${capture.surface} · ${capture.geometry} · ${capture.width}×${capture.height}`;
  image.src = `/image?id=${capture.id}`;
  image.alt = `${capture.surface} at ${capture.geometry}`;
  overlay.hidden = false;
}

function closeZoom() {
  document.getElementById("zoom").hidden = true;
}

// --- Wiring ----------------------------------------------------------------

document.getElementById("add-tag").addEventListener("click", async () => {
  const name = window.prompt("New tag:");

  if (!name || !name.trim()) return;

  await api("/api/tags", { name: name.trim() });
  await reload();
});

// Closing the zoom leaves scroll position and selection untouched, because it
// is an overlay over the same grid rather than a page of its own.
document.getElementById("zoom").addEventListener("click", closeZoom);
window.addEventListener("keydown", (event) => {
  if (event.key === "Escape") closeZoom();
});

bandSelect();
reload();
