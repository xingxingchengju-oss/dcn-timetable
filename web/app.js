// =============================================================================
// Timetable Inquiry System — web frontend
//
// Bridge URL must match bridge.py BRIDGE_PORT.
// All TCP/encryption work happens in bridge.py + C++ server; this script never
// touches XOR_KEY. The Wire Preview shows raw_request / raw_response strings
// returned by the bridge, so what users see is exactly what crossed the wire.
// =============================================================================

const BRIDGE = "http://127.0.0.1:50002";

const ERROR_MESSAGES = {
  E101: "Missing or invalid input",
  E102: "Unknown command",
  E201: "Please log in first",
  E202: "Admin privileges required",
  E301: "Course already exists",
  E302: "Course not found",
  E303: "Invalid field name",
  E500: "Server internal error",
};

const DAY_ORDER = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];
const DAY_LABEL = {
  Mon: "Monday",   Tue: "Tuesday", Wed: "Wednesday", Thu: "Thursday",
  Fri: "Friday",   Sat: "Saturday", Sun: "Sunday",
};

const state = {
  sessionId: null,
  role: "guest",
  username: "",
  encryption: false,
  mode: "quick",       // "quick" | "advanced"
  quickTab: "code",    // "code" | "instructor" | "time" | "all"
  view: "table",       // "table" | "week"
  rows: [],            // last query results
  lastQueryDesc: "",   // headline phrase
  lastFilters: [],     // chips for the headline area
  lastLatencyMs: 0,
  editing: { code: "", section: "", field: "" },
  deleting: { code: "", section: "" },
  detailRow: null,
  connection: "connecting", // "connected" | "connecting" | "disconnected"
};

// =============================================================================
// Connection state — sidebar pill, main meta row, empty/offline state, gating
// =============================================================================

function setConnectionState(s) {
  if (state.connection === s) return;
  state.connection = s;
  const block = document.querySelector(".status-block");
  if (block) block.dataset.conn = s;
  const count = document.getElementById("conn-count");
  const uptime = document.getElementById("conn-uptime");
  const total = document.getElementById("conn-total");

  if (s === "connecting" || s === "disconnected") {
    if (count) count.textContent = "—/64";
    if (uptime) uptime.textContent = "—";
    if (total) total.textContent = "—";
  }

  updateMetaForConnection();
  updateOfflineCard();
  gateQueryButtons();
}

function updateMetaForConnection() {
  const meta = document.getElementById("result-meta");
  const text = document.getElementById("result-meta-text");
  if (!meta || !text) return;
  if (state.connection === "disconnected") {
    meta.classList.add("is-offline");
    text.textContent = "Server unreachable · click to retry";
  } else {
    meta.classList.remove("is-offline");
    // text gets refreshed by setHeadline / clearResults; leave alone unless empty
  }
}

function updateOfflineCard() {
  const card = document.querySelector(".table-card");
  if (!card) return;
  if (state.connection === "disconnected" && state.rows.length === 0) {
    card.classList.add("offline");
    card.classList.remove("empty");
  } else {
    card.classList.remove("offline");
    if (state.rows.length === 0) card.classList.add("empty");
  }
}

function gateQueryButtons() {
  const offline = state.connection !== "connected";
  const tooltip = offline ? "Connect to server first" : "";
  ["run-query-btn", "add-btn"].forEach(id => {
    const b = document.getElementById(id);
    if (!b) return;
    b.disabled = offline;
    b.title = tooltip;
  });
}

async function retryConnect() {
  setConnectionState("connecting");
  try {
    const resp = await fetch(`${BRIDGE}/api/connect`, { method: "POST" });
    const data = await resp.json();
    if (data.ok) {
      state.sessionId = data.session_id;
      const sidEl = document.getElementById("session-id");
      if (sidEl) sidEl.textContent = data.session_id ? data.session_id.slice(0, 4).toUpperCase() : "—";
      const lines = data.lines || [];
      if (lines.length && lines[0].startsWith("WELCOME|")) {
        const banner = lines[0].split("|").slice(1).join(" — ");
        showToast(banner, "info");
      }
      renderWire("(connected)", lines.join("\n") + "\n");
      setConnectionState("connected");
      pollStatus();
      return true;
    } else {
      setConnectionState("disconnected");
      showToast("Cannot connect to server: " + (data.error || "unknown"), "error");
      return false;
    }
  } catch (_) {
    setConnectionState("disconnected");
    showToast("Cannot reach bridge — is bridge.py running on port 50002?", "error");
    return false;
  }
}

// =============================================================================
// Toast notifications
// =============================================================================

const _toastTimers = new Map();

function showToast(msg, type = "info", duration = 0) {
  const container = document.getElementById("toast-container");
  const toast = document.createElement("div");
  toast.className = `toast toast-${type}`;

  const msgEl = document.createElement("span");
  msgEl.className = "toast-msg";
  msgEl.textContent = msg;

  const closeBtn = document.createElement("button");
  closeBtn.className = "toast-close";
  closeBtn.setAttribute("aria-label", "Dismiss");
  closeBtn.textContent = "×";
  closeBtn.addEventListener("click", () => dismissToast(toast));

  toast.appendChild(msgEl);
  toast.appendChild(closeBtn);
  container.appendChild(toast);

  const ms = duration || ((type === "error" || type === "warning") ? 6000 : 4000);
  _toastTimers.set(toast, setTimeout(() => dismissToast(toast), ms));

  toast.addEventListener("mouseenter", () => {
    clearTimeout(_toastTimers.get(toast));
    _toastTimers.delete(toast);
  });
  toast.addEventListener("mouseleave", () => {
    _toastTimers.set(toast, setTimeout(() => dismissToast(toast), 2500));
  });
}

function dismissToast(toast) {
  if (!toast.parentNode) return;
  clearTimeout(_toastTimers.get(toast));
  _toastTimers.delete(toast);
  toast.classList.add("toast-exit");
  toast.addEventListener("animationend", () => toast.remove(), { once: true });
  setTimeout(() => { if (toast.parentNode) toast.remove(); }, 400);
}

function friendlyError(code) {
  const desc = ERROR_MESSAGES[code];
  return desc ? `${desc} (${code})` : code || "Unknown error";
}

// =============================================================================
// Button loading state
// =============================================================================

async function withLoading(button, asyncFn) {
  if (!button) return asyncFn();
  const label = button.innerHTML;
  const minW  = button.offsetWidth;
  button.style.minWidth = minW + "px";
  button.disabled = true;
  button.innerHTML = `<span class="btn-spinner"></span>`;
  try {
    return await asyncFn();
  } finally {
    button.disabled = false;
    button.innerHTML = label;
    button.style.minWidth = "";
  }
}

// =============================================================================
// Bridge fetch wrapper
// =============================================================================

function isSha256Hex(s) {
  return /^[0-9a-f]{64}$/.test(s);
}
async function sha256Hex(s) {
  const buf = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s));
  return Array.from(new Uint8Array(buf))
    .map(b => b.toString(16).padStart(2, "0"))
    .join("");
}

// Send a command to the bridge. Bridge handles LOGIN hashing and ENC| wrapping
// internally — but to keep the Wire Preview useful, we hash LOGIN passwords here
// too (so the wire shows the actual hashed payload that hits the server, not the
// plaintext password the user typed).
// Silently establish a fresh session. Used by call()'s retry path so the
// user never sees a flicker when a TCP socket dies mid-demo.
async function silentReconnect() {
  try {
    const resp = await fetch(`${BRIDGE}/api/connect`, { method: "POST" });
    const data = await resp.json();
    if (!data.ok) return false;
    state.sessionId = data.session_id;
    const sidEl = document.getElementById("session-id");
    if (sidEl) sidEl.textContent = data.session_id ? data.session_id.slice(0, 4).toUpperCase() : "—";
    setConnectionState("connected");
    return true;
  } catch (_) {
    return false;
  }
}

async function call(cmd, _isRetry = false) {
  const t0 = performance.now();
  let resp, data;
  try {
    resp = await fetch(`${BRIDGE}/api/command`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ session_id: state.sessionId, command: cmd }),
    });
    data = await resp.json();
  } catch (err) {
    // Network-level failure (bridge down or refused). Don't flicker offline yet —
    // give it ONE silent retry first. If that fails, then we're really offline.
    if (!_isRetry) {
      const ok = await silentReconnect();
      if (ok) return call(cmd, true);
    }
    setConnectionState("disconnected");
    showToast("Cannot reach bridge — is bridge.py running?", "error");
    return [];
  }

  state.lastLatencyMs = Math.round(performance.now() - t0);

  // Bridge says ok — happy path.
  if (data.ok) {
    if (state.connection !== "connected") setConnectionState("connected");
    renderWire(data.raw_request, data.raw_response);
    return data.lines || [];
  }

  // Bridge returned an error. The most common transient case is "Unknown session"
  // (HTTP 400) when bridge.py was restarted. Try once to re-establish silently.
  const isSessionError =
    resp.status === 400 ||
    /unknown session|invalid session|connection|unreachable/i.test(data.error || "");

  if (!_isRetry && isSessionError) {
    const ok = await silentReconnect();
    if (ok) return call(cmd, true);
  }

  renderWire(data.raw_request || cmd, data.raw_response || "(no response)");
  if (isSessionError) {
    setConnectionState("disconnected");
    // Even though the offline card is now visible, surface a one-shot toast
    // so the user gets a clear "I tried to recover, it didn't work" signal.
    showToast(
      _isRetry
        ? "Reconnect failed — server may be down."
        : (data.error || "Connection error"),
      "error"
    );
  } else {
    showToast(data.error || "Bridge error", "error");
  }
  return [];
}

// =============================================================================
// Wire Preview
// =============================================================================

function renderWire(req, resp) {
  document.getElementById("wire-request").textContent = req || "(empty)";
  document.getElementById("wire-response").textContent = resp || "(empty)";
  updateWireMode();
  const now = new Date();
  document.getElementById("wire-time").textContent =
    "last frame · " +
    now.toTimeString().slice(0, 8) + "." + String(now.getMilliseconds()).padStart(3, "0");
}

function updateWireMode() {
  const el = document.getElementById("wire-mode");
  if (!el) return;
  if (state.encryption) {
    el.textContent = "ENC";
    el.title = "XOR encryption on — payloads are XOR'd with the shared key and hex-encoded.";
    el.classList.add("is-enc");
  } else {
    el.textContent = "PLAINTEXT";
    el.title = "Encryption off — payloads are sent in cleartext. Toggle XOR Encryption in the sidebar.";
    el.classList.remove("is-enc");
  }
}

function toggleWire() {
  const drawer = document.getElementById("wire-drawer");
  const isCollapsed = drawer.classList.toggle("collapsed");
  document.getElementById("wire-toggle").setAttribute("aria-expanded", String(!isCollapsed));
}

// =============================================================================
// Rendering — table
// =============================================================================

function clearTable() {
  document.getElementById("result-body").innerHTML = "";
  state.rows = [];
  updateOfflineCard();
}

function setHeadline(desc, filters = [], rowCount = null, source = "live") {
  const h = document.getElementById("result-headline");
  if (rowCount !== null && desc) {
    h.innerHTML = `${rowCount} ${rowCount === 1 ? "result" : "results"} <em>for "${escapeHtml(desc)}"</em>`;
  } else if (desc) {
    h.textContent = desc;
  } else {
    h.textContent = "Timetable";
  }

  const chipBox = document.getElementById("filter-chips");
  chipBox.innerHTML = "";
  filters.forEach(f => {
    const chip = document.createElement("span");
    chip.className = "filter-chip";
    chip.textContent = f;
    chipBox.appendChild(chip);
  });

  const text = document.getElementById("result-meta-text");
  if (state.connection === "disconnected") {
    text.textContent = "Server unreachable · click to retry";
  } else if (rowCount !== null) {
    const ms = state.lastLatencyMs ? `${state.lastLatencyMs}ms` : "—";
    const tag = state.encryption ? "encrypted" : source;
    text.textContent = `${rowCount} results · ${ms} · ${tag}`;
  } else {
    text.textContent = "Run a query to see results";
  }
}

function renderRows(rows) {
  state.rows = rows;
  const tbody = document.getElementById("result-body");
  tbody.innerHTML = "";
  const card = document.querySelector(".table-card");
  card.classList.remove("offline");

  if (!rows.length) {
    card.classList.add("empty");
    return;
  }
  card.classList.remove("empty");

  rows.forEach(r => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="col-code">${escapeHtml(r.code)}</td>
      <td>${escapeHtml(r.title)}</td>
      <td class="col-section">${escapeHtml(r.section)}</td>
      <td>${escapeHtml(r.instructor)}</td>
      <td>${escapeHtml(r.day)}</td>
      <td>${escapeHtml(r.time)}</td>
      <td>${escapeHtml(r.duration)}</td>
      <td>${escapeHtml(r.classroom)}</td>
      <td>${escapeHtml(r.semester)}</td>
      <td class="admin-col" data-admin-only>
        <div class="row-actions">
          <button class="row-action-btn" data-edit="${escapeHtml(r.code)}|${escapeHtml(r.section)}">✎</button>
          <button class="row-action-btn danger" data-delete="${escapeHtml(r.code)}|${escapeHtml(r.section)}">🗑</button>
        </div>
      </td>`;
    tbody.appendChild(tr);
  });

  tbody.querySelectorAll("[data-edit]").forEach(btn => {
    btn.addEventListener("click", () => {
      const [code, section] = btn.dataset.edit.split("|");
      openEditModal(code, section);
    });
  });
  tbody.querySelectorAll("[data-delete]").forEach(btn => {
    btn.addEventListener("click", () => {
      const [code, section] = btn.dataset.delete.split("|");
      openDeleteModal(code, section);
    });
  });

  if (state.view === "week") renderWeek();
}

function escapeHtml(s) {
  return String(s ?? "")
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}

// Parse one RESULT|... protocol line into a row object.
function parseResultLine(line) {
  const fields = line.split("|").slice(1);
  const [code, title, section, instructor, day, time, duration, classroom, semester] = fields;
  return { code, title, section, instructor, day, time, duration, classroom, semester };
}

// =============================================================================
// Rendering — week view
// =============================================================================

// Returns duration in HOURS (float). Supports "2h", "1.5h", "90min", "45min".
function parseDurationHours(s) {
  if (!s) return 1;
  const m = String(s).trim().match(/^(\d+(?:\.\d+)?)\s*(h|hr|hour|hours|min|m|minute|minutes)$/i);
  if (!m) return 1;
  const n = parseFloat(m[1]);
  const unit = m[2].toLowerCase();
  return unit.startsWith("h") ? n : n / 60;
}
// Back-compat (returns minutes); kept in case any caller uses it.
function parseDuration(s) {
  return Math.round(parseDurationHours(s) * 60);
}

function parseStartHour(time) {
  if (!time) return null;
  const m = String(time).trim().match(/^(\d{1,2}):(\d{2})$/);
  if (!m) return null;
  return { h: parseInt(m[1], 10), min: parseInt(m[2], 10) };
}

function hashColor(code) {
  let h = 0;
  for (const c of code) h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return (h % 6) + 1;
}

function renderWeek() {
  const grid = document.getElementById("week-grid");
  grid.innerHTML = "";

  // Row 1: top-left corner + 7 day headers
  const corner = document.createElement("div");
  corner.className = "week-corner";
  grid.appendChild(corner);
  DAY_ORDER.forEach((d, i) => {
    const h = document.createElement("div");
    h.className = "week-day-header";
    h.style.gridColumn = `${2 + i} / ${3 + i}`;
    h.textContent = d;
    grid.appendChild(h);
  });

  // Col 1: 14 hour labels at rows 2..15
  for (let i = 0; i < 14; i++) {
    const hour = 8 + i;
    const lbl = document.createElement("div");
    lbl.className = "week-time-label";
    lbl.style.gridRow = `${2 + i} / ${3 + i}`;
    lbl.textContent = `${String(hour).padStart(2, "0")}:00`;
    grid.appendChild(lbl);
  }

  // Day columns: vertical gridlines (rows 2..-1 each)
  for (let i = 0; i < 7; i++) {
    const col = document.createElement("div");
    col.className = "week-day-col";
    col.style.gridColumn = `${2 + i} / ${3 + i}`;
    grid.appendChild(col);
  }

  // Place events
  state.rows.forEach(r => {
    const dayIdx = DAY_ORDER.indexOf(r.day);
    if (dayIdx < 0) return;
    const start = parseStartHour(r.time);
    if (!start) return;
    if (start.h < 8 || start.h >= 22) return;

    const durHours = parseDurationHours(r.duration);
    const span = Math.max(1, Math.ceil(durHours)); // grid rows to occupy
    const rowStart = 2 + (start.h - 8);            // row 1 = header
    const rowEnd = Math.min(16, rowStart + span);  // last hour row is 15 → end at 16
    const colStart = 2 + dayIdx;

    const ev = document.createElement("div");
    ev.className = `week-event color-${hashColor(r.code)}`;
    ev.style.gridColumn = `${colStart} / ${colStart + 1}`;
    ev.style.gridRow = `${rowStart} / ${rowEnd}`;
    // Offset within the cell when start time isn't on the hour
    if (start.min) {
      ev.style.marginTop = `${(start.min / 60) * 56}px`;
    }
    // Constrain card height to actual duration when span rounded up
    if (durHours < span) {
      // total available = span * 56 - margins (top offset already applied)
      const offsetPx = (start.min || 0) / 60 * 56;
      const desired  = durHours * 56 - 2; // tiny gap below
      const available = span * 56 - offsetPx;
      if (desired < available) ev.style.marginBottom = `${available - desired}px`;
    }
    ev.title = `${r.code} ${r.section} · ${r.title}\n${r.instructor}\n${r.day} ${r.time} (${r.duration})\n${r.classroom} · ${r.semester}`;

    const head = document.createElement("div");
    head.className = "week-event-head";
    head.textContent = `${r.code} · ${r.section}`;

    const title = document.createElement("div");
    title.className = "week-event-title";
    title.textContent = r.title;

    const meta = document.createElement("div");
    meta.className = "week-event-meta";
    const fmtMin = start.min ? `:${String(start.min).padStart(2, "0")}` : ":00";
    meta.textContent = `${String(start.h).padStart(2, "0")}${fmtMin} · ${r.duration}`;

    ev.appendChild(head);
    ev.appendChild(title);
    ev.appendChild(meta);

    ev.addEventListener("click", () => openDetailModal(r));

    grid.appendChild(ev);
  });
}

// =============================================================================
// Result handling — generic
// =============================================================================

function handleQueryResponse(lines, desc, filters, source = "live") {
  if (!lines.length) {
    setHeadline("(no response)", filters, 0, source);
    clearTable();
    return;
  }
  const first = lines[0];

  if (first === "RESULT_BEGIN") {
    const rows = lines
      .filter(l => l.startsWith("RESULT|"))
      .map(parseResultLine);
    setHeadline(desc, filters, rows.length, source);
    if (!rows.length) showToast("No courses found.", "warning");
    renderRows(rows);
    return;
  }

  if (first.startsWith("RESULT_NONE|")) {
    setHeadline(desc, filters, 0, source);
    clearTable();
    showToast("No courses found.", "warning");
    return;
  }

  if (first.startsWith("FAILURE|") || first.startsWith("ERROR|")) {
    const [, code, ...rest] = first.split("|");
    showToast(friendlyError(code) + (rest.length ? ": " + rest.join("|") : ""), "error");
    return;
  }

  // Unexpected line — don't toast raw protocol strings; log quietly.
  console.warn("[query] unexpected first line:", first);
}

function handleWriteResponse(lines, successMsg, onSuccess) {
  if (!lines.length) return;
  const first = lines[0];
  if (first.startsWith("OK|")) {
    showToast(successMsg, "success");
    if (onSuccess) onSuccess();
    return;
  }
  if (first.startsWith("FAILURE|") || first.startsWith("ERROR|")) {
    const [, code, ...rest] = first.split("|");
    showToast(friendlyError(code) + (rest.length ? ": " + rest.join("|") : ""), "error");
    return;
  }
  showToast(first, "info");
}

// =============================================================================
// Sidebar — mode + tab switching
// =============================================================================

function setMode(mode) {
  state.mode = mode;
  document.querySelectorAll(".mode-btn").forEach(b =>
    b.classList.toggle("active", b.dataset.mode === mode));
  document.querySelectorAll("[data-panel]").forEach(p =>
    p.classList.toggle("hidden", p.dataset.panel !== mode));
}

function setQuickTab(tab) {
  state.quickTab = tab;
  document.querySelectorAll(".quick-tab").forEach(b =>
    b.classList.toggle("active", b.dataset.qtab === tab));
  document.querySelectorAll("[data-qbody]").forEach(b =>
    b.classList.toggle("hidden", b.dataset.qbody !== tab));
}

function setView(view) {
  state.view = view;
  document.querySelectorAll(".view-btn").forEach(b =>
    b.classList.toggle("active", b.dataset.view === view));
  document.querySelectorAll("[data-view-pane]").forEach(p =>
    p.classList.toggle("hidden", p.dataset.viewPane !== view));
  if (view === "week") renderWeek();
}

// chip-group: single-select; clicking same chip deselects
function bindChipGroup(containerId, onChange) {
  const container = document.getElementById(containerId);
  container.querySelectorAll(".chip").forEach(chip => {
    chip.addEventListener("click", () => {
      const wasActive = chip.classList.contains("active");
      container.querySelectorAll(".chip").forEach(c => c.classList.remove("active"));
      if (!wasActive) chip.classList.add("active");
      const active = container.querySelector(".chip.active");
      onChange(active ? active.dataset.value : "");
    });
  });
  // Default-select the first chip (whose data-value is "")
  const first = container.querySelector(".chip");
  if (first) first.classList.add("active");
}

// =============================================================================
// Run query — dispatches based on current mode/tab
// =============================================================================

async function runQuery() {
  const btn = document.getElementById("run-query-btn");
  await withLoading(btn, async () => {
    if (state.mode === "quick") {
      await runQuick();
    } else {
      await runAdvanced();
    }
  });
}

async function runQuick() {
  const tab = state.quickTab;
  if (tab === "code") {
    const code = document.getElementById("q-code").value.trim();
    if (!code) return showToast("Please enter a course code.", "warning");
    handleQueryResponse(await call(`QUERY|${code}`), code,
      [`code = ${code}`]);
  } else if (tab === "instructor") {
    const name = document.getElementById("q-instructor").value.trim();
    if (!name) return showToast("Please enter an instructor name.", "warning");
    handleQueryResponse(await call(`SEARCH_INSTRUCTOR|${name}`), name,
      [`instructor ~ ${name}`]);
  } else if (tab === "time") {
    const day = document.getElementById("q-day").value;
    const time = document.getElementById("q-time").value.trim();
    if (!time) return showToast("Please enter a time (HH:MM).", "warning");
    if (!/^\d{1,2}:\d{2}$/.test(time)) {
      return showToast("Time must be exact HH:MM (e.g. 10:00). For ranges, use Advanced.", "warning");
    }
    handleQueryResponse(await call(`SEARCH_TIME|${day}|${time}`), `${day} ${time}`,
      [`day = ${day}`, `time = ${time}`]);
  } else if (tab === "all") {
    const sem = document.getElementById("q-semester").value.trim();
    const cmd = sem ? `LIST_ALL|${sem}` : "LIST_ALL";
    handleQueryResponse(await call(cmd), sem ? `all in ${sem}` : "all courses",
      sem ? [`semester = ${sem}`] : []);
  }
}

async function runAdvanced() {
  const kw  = document.getElementById("adv-keyword").value.trim();
  const sem = document.getElementById("adv-semester").value.trim();
  const day = document.querySelector("#adv-days .chip.active")?.dataset.value || "";
  const tr  = document.querySelector("#adv-time-range .chip.active")?.dataset.value || "";

  const parts = ["SEARCH_ADVANCED"];
  if (kw)  parts.push(`keyword=${kw}`);
  if (day) parts.push(`day=${day}`);
  if (sem) parts.push(`semester=${sem}`);
  if (tr)  parts.push(`time_range=${tr}`);

  const filters = [];
  if (kw)  filters.push(`keyword ~ ${kw}`);
  if (day) filters.push(`day = ${day}`);
  if (sem) filters.push(`semester = ${sem}`);
  if (tr)  filters.push(`time = ${tr}`);

  if (parts.length === 1) {
    return showToast("Add at least one filter (keyword, day, semester, or time range).", "warning");
  }

  const desc = kw || filters.join(", ") || "filtered";
  handleQueryResponse(await call(parts.join("|")), desc, filters);
}

function clearResults() {
  document.getElementById("result-headline").textContent = "Timetable";
  document.getElementById("filter-chips").innerHTML = "";
  const text = document.getElementById("result-meta-text");
  if (text) {
    text.textContent = state.connection === "disconnected"
      ? "Server unreachable · click to retry"
      : "Run a query to see results";
  }
  clearTable();
  updateOfflineCard();
  if (state.view === "week") renderWeek();
}

// =============================================================================
// Auth
// =============================================================================

function setRole(r, username = "") {
  state.role = r;
  state.username = username;
  document.body.dataset.role = r;
  document.getElementById("auth-username").textContent = username || "—";
  document.getElementById("auth-role").textContent = r;
  document.getElementById("auth-initial").textContent = (username[0] || "?").toUpperCase();
}

function openLoginModal() { openModal("login-modal"); setTimeout(() => document.getElementById("username")?.focus(), 50); }
async function login(event) {
  if (event) event.preventDefault();
  const user = document.getElementById("username").value.trim();
  const pass = document.getElementById("password").value;
  if (!user || !pass) return showToast("Please enter username and password.", "warning");

  const submit = document.querySelector("#login-form button[type='submit']");
  await withLoading(submit, async () => {
    // Hash on the client too so the wire never carries the plaintext password.
    const hashed = isSha256Hex(pass) ? pass : await sha256Hex(pass);
    const lines = await call(`LOGIN|${user}|${hashed}`);
    if (!lines.length) return;
    const first = lines[0];
    if (first.startsWith("SUCCESS|")) {
      const newRole = first.split("|")[1] || "student";
      setRole(newRole, user);
      closeModal("login-modal");
      document.getElementById("password").value = "";
      showToast(`Signed in as ${user} (${newRole}).`, "success");
    } else {
      const [, code, ...rest] = first.split("|");
      showToast(friendlyError(code) + (rest.length ? ": " + rest.join("|") : ""), "error");
    }
  });
}

async function logout() {
  await call("LOGOUT");
  setRole("guest");
  showToast("Logged out.", "info");
}

// =============================================================================
// Modal helpers
// =============================================================================

function openModal(id) {
  const m = document.getElementById(id);
  m.classList.add("open");
  m.setAttribute("aria-hidden", "false");
}
function closeModal(id) {
  const m = document.getElementById(id);
  m.classList.remove("open");
  m.setAttribute("aria-hidden", "true");
}
function closeAllModals() {
  document.querySelectorAll(".modal.open").forEach(m => closeModal(m.id));
}

// =============================================================================
// Add course
// =============================================================================

async function submitAdd(event) {
  event.preventDefault();
  const ids = ["a-code","a-title","a-section","a-instructor","a-day","a-time","a-duration","a-classroom","a-semester"];
  const values = ids.map(id => document.getElementById(id).value.trim());
  if (values.some(v => !v)) return showToast("All 9 fields are required.", "warning");

  const btn = document.querySelector("#add-form button[type='submit']");
  await withLoading(btn, async () => {
    handleWriteResponse(
      await call("ADD|" + values.join("|")),
      `Course ${values[0]} (${values[2]}) added.`,
      () => {
        closeModal("add-modal");
        document.getElementById("add-form").reset();
        // Refresh current view if user was looking at LIST_ALL or similar
        if (state.rows.length) refreshAfterMutation();
      }
    );
  });
}

// =============================================================================
// Edit course (two-step single-field flow)
// =============================================================================

function openDetailModal(row) {
  state.detailRow = row;
  ["code", "title", "section", "instructor", "day", "time", "duration", "classroom", "semester"]
    .forEach(f => {
      const el = document.getElementById(`det-${f}`);
      if (el) el.textContent = row[f] || "—";
    });
  openModal("detail-modal");
}

function openEditModal(code, section) {
  state.editing = { code, section, field: "" };
  document.getElementById("edit-target").textContent = `${code} · ${section}`;
  // Reset to step 1
  document.querySelectorAll("#edit-modal .edit-step").forEach(s =>
    s.classList.toggle("hidden", s.dataset.step !== "1"));
  document.querySelectorAll("#edit-fields .chip").forEach(c => c.classList.remove("active"));
  document.getElementById("edit-value").value = "";
  openModal("edit-modal");
}

function pickEditField(field) {
  state.editing.field = field;
  document.getElementById("edit-field-name").textContent = field;
  document.querySelectorAll("#edit-modal .edit-step").forEach(s =>
    s.classList.toggle("hidden", s.dataset.step !== "2"));
  // Pre-fill with current value if we have it
  const row = state.rows.find(r =>
    r.code === state.editing.code && r.section === state.editing.section);
  if (row && row[field] !== undefined) {
    document.getElementById("edit-value").value = row[field];
  }
  setTimeout(() => document.getElementById("edit-value").focus(), 50);
}

function backToFieldPick() {
  document.querySelectorAll("#edit-modal .edit-step").forEach(s =>
    s.classList.toggle("hidden", s.dataset.step !== "1"));
}

async function submitEdit(event) {
  event.preventDefault();
  const { code, section, field } = state.editing;
  const value = document.getElementById("edit-value").value.trim();
  if (!field) return showToast("Pick a field first.", "warning");
  if (!value) return showToast("New value cannot be empty.", "warning");

  const btn = document.querySelector("#edit-form button[type='submit']");
  await withLoading(btn, async () => {
    handleWriteResponse(
      await call(`UPDATE|${code}|${section}|${field}|${value}`),
      `Updated ${code} · ${section}: ${field} → ${value}`,
      () => {
        // Stay open so user can chain another field; just go back to step 1
        backToFieldPick();
        document.querySelectorAll("#edit-fields .chip").forEach(c => c.classList.remove("active"));
        refreshAfterMutation();
      }
    );
  });
}

// =============================================================================
// Delete course
// =============================================================================

function openDeleteModal(code, section) {
  state.deleting = { code, section };
  document.getElementById("delete-target").textContent = `${code} · ${section}`;
  openModal("delete-modal");
}

async function confirmDelete() {
  const { code, section } = state.deleting;
  const btn = document.getElementById("delete-confirm-btn");
  await withLoading(btn, async () => {
    handleWriteResponse(
      await call(`DELETE|${code}|${section}`),
      `Deleted ${code} · ${section}.`,
      () => {
        closeModal("delete-modal");
        refreshAfterMutation();
      }
    );
  });
}

// After ADD/UPDATE/DELETE, re-run the last query so the table reflects truth.
async function refreshAfterMutation() {
  // Simplest reliable refresh: re-run the current sidebar query if any.
  // If there's no active query state, just leave the table alone.
  if (state.mode === "quick" && state.quickTab === "all") {
    await runQuick();
  } else if (state.mode === "advanced") {
    await runAdvanced();
  } else if (state.mode === "quick") {
    // For specific quick searches re-run them too
    await runQuick();
  }
}

// =============================================================================
// Encryption toggle
// =============================================================================

async function toggleEncryption(event) {
  const enabled = event.target.checked;
  try {
    const resp = await fetch(`${BRIDGE}/api/encryption`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ session_id: state.sessionId, enabled }),
    });
    const data = await resp.json();
    if (!data.ok) {
      event.target.checked = !enabled;
      showToast(data.error || "Failed to set encryption", "error");
      return;
    }
    state.encryption = enabled;
    updateWireMode();
    showToast(`XOR encryption ${enabled ? "enabled" : "disabled"}.`, "info");
  } catch (err) {
    event.target.checked = !enabled;
    showToast("Network error: " + err.message, "error");
  }
}

// =============================================================================
// Server status polling
// =============================================================================

async function pollStatus() {
  if (!state.sessionId) return;
  try {
    const resp = await fetch(`${BRIDGE}/api/status?session_id=${state.sessionId}`);
    // 503 = session busy with a query — skip this cycle, not a real error.
    if (resp.status === 503) return;
    const data = await resp.json();
    if (!data.ok) {
      // Only go offline if the session is truly gone (400), not on transient errors.
      if (resp.status === 400) setConnectionState("disconnected");
      return;
    }
    if (state.connection !== "connected") setConnectionState("connected");
    const countEl = document.getElementById("conn-count");
    if (countEl) countEl.textContent = `${data.active || 0}/64`;
    const upEl = document.getElementById("conn-uptime");
    if (upEl) upEl.textContent = data.uptime || "—";
    const totEl = document.getElementById("conn-total");
    if (totEl) totEl.textContent = (data.total ?? 0).toLocaleString();
  } catch (_) {
    // Network-level failure (bridge down) — go offline.
    setConnectionState("disconnected");
  }
}

// =============================================================================
// Init
// =============================================================================

document.addEventListener("DOMContentLoaded", async () => {
  // Mode + quick tab switches
  document.querySelectorAll(".mode-btn").forEach(b =>
    b.addEventListener("click", () => setMode(b.dataset.mode)));
  document.querySelectorAll(".quick-tab").forEach(b =>
    b.addEventListener("click", () => setQuickTab(b.dataset.qtab)));
  document.querySelectorAll(".view-btn").forEach(b =>
    b.addEventListener("click", () => setView(b.dataset.view)));

  // "Switch to Advanced" link from Quick → Day+Time
  document.querySelectorAll("[data-goto-advanced]").forEach(a =>
    a.addEventListener("click", e => { e.preventDefault(); setMode("advanced"); }));

  // Chip groups
  bindChipGroup("adv-days", () => {});
  bindChipGroup("adv-time-range", () => {});

  // Edit modal field chips
  document.querySelectorAll("#edit-fields .chip").forEach(chip => {
    chip.addEventListener("click", () => {
      document.querySelectorAll("#edit-fields .chip").forEach(c => c.classList.remove("active"));
      chip.classList.add("active");
      pickEditField(chip.dataset.field);
    });
  });
  document.getElementById("edit-back-btn").addEventListener("click", backToFieldPick);
  document.getElementById("edit-form").addEventListener("submit", submitEdit);

  // Buttons
  document.getElementById("run-query-btn").addEventListener("click", runQuery);
  document.getElementById("clear-btn").addEventListener("click", clearResults);
  document.getElementById("open-login-btn").addEventListener("click", openLoginModal);
  document.getElementById("logout-btn").addEventListener("click", logout);
  document.getElementById("login-form").addEventListener("submit", login);
  document.getElementById("add-btn").addEventListener("click", () => openModal("add-modal"));
  document.getElementById("add-form").addEventListener("submit", submitAdd);
  document.getElementById("delete-confirm-btn").addEventListener("click", confirmDelete);
  document.getElementById("enc-toggle").addEventListener("change", toggleEncryption);
  document.getElementById("wire-toggle").addEventListener("click", toggleWire);

  // Modal close handlers
  document.querySelectorAll("[data-close-modal]").forEach(el =>
    el.addEventListener("click", () => closeAllModals()));
  document.addEventListener("keydown", e => {
    if (e.key === "Escape") closeAllModals();
  });

  // Enter-key triggers Run query when any sidebar input has focus
  document.querySelectorAll(".sidebar input").forEach(input => {
    input.addEventListener("keydown", e => {
      if (e.key === "Enter") { e.preventDefault(); runQuery(); }
    });
  });

  // Detail modal — Edit and Delete actions
  document.getElementById("detail-edit-btn")?.addEventListener("click", () => {
    if (!state.detailRow) return;
    closeModal("detail-modal");
    openEditModal(state.detailRow.code, state.detailRow.section);
  });
  document.getElementById("detail-delete-btn")?.addEventListener("click", () => {
    if (!state.detailRow) return;
    closeModal("detail-modal");
    openDeleteModal(state.detailRow.code, state.detailRow.section);
  });

  // Retry / how-to buttons in the offline card + meta row click
  document.getElementById("retry-btn")?.addEventListener("click", retryConnect);
  document.getElementById("howto-btn")?.addEventListener("click", () => {
    document.getElementById("offline-howto")?.classList.toggle("hidden");
  });
  document.getElementById("result-meta")?.addEventListener("click", () => {
    if (state.connection === "disconnected") retryConnect();
  });

  // Initialise meta + wire mode
  updateWireMode();
  setConnectionState("connecting");

  // Establish connection
  await retryConnect();
  setInterval(pollStatus, 5000);

  // Set initial meta text (only if still empty)
  const text = document.getElementById("result-meta-text");
  if (text && !text.textContent) text.textContent = "Run a query to see results";
});

// Graceful disconnect on tab close. URLSearchParams default Content-Type is
// application/x-www-form-urlencoded, which bridge.py /api/disconnect handles.
window.addEventListener("beforeunload", () => {
  if (!state.sessionId) return;
  navigator.sendBeacon(
    `${BRIDGE}/api/disconnect`,
    new URLSearchParams({ session_id: state.sessionId })
  );
});
