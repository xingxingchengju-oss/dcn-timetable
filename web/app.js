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

let sessionId = null;
let role = "guest";

// Element that opened the login modal — for focus restoration on close.
let lastFocusedTrigger = null;

// =====================================================================
// State helpers
// =====================================================================

function setRole(r) {
  role = r;
  document.body.dataset.role = r;
  document.getElementById("role-badge").textContent = r;
}

function clearTable() {
  document.getElementById("result-body").innerHTML = "";
  document.getElementById("results-card").classList.remove("has-rows");
  document.getElementById("summary-bar").textContent = "";
}

function friendlyError(code) {
  const desc = ERROR_MESSAGES[code];
  return desc ? `${desc} (${code})` : code;
}

// =====================================================================
// Toast notifications
// =====================================================================

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

  // Auto-dismiss: 6 s for error/warning, 4 s for everything else
  const ms = duration || ((type === "error" || type === "warning") ? 6000 : 4000);
  _toastTimers.set(toast, setTimeout(() => dismissToast(toast), ms));

  // Hover-pause: clear timer on enter, restart with 2.5 s on leave
  toast.addEventListener("mouseenter", () => {
    clearTimeout(_toastTimers.get(toast));
    _toastTimers.delete(toast);
  });
  toast.addEventListener("mouseleave", () => {
    _toastTimers.set(toast, setTimeout(() => dismissToast(toast), 2500));
  });

  return toast;
}

function dismissToast(toast) {
  if (!toast.parentNode) return;
  clearTimeout(_toastTimers.get(toast));
  _toastTimers.delete(toast);
  toast.classList.add("toast-exit");
  const remove = () => { if (toast.parentNode) toast.remove(); };
  toast.addEventListener("animationend", remove, { once: true });
  setTimeout(remove, 400); // fallback
}

function clearAllToasts() {
  document.querySelectorAll(".toast").forEach(t => {
    clearTimeout(_toastTimers.get(t));
    _toastTimers.delete(t);
    t.remove();
  });
}

// =====================================================================
// Button loading state
// =====================================================================

async function withLoading(button, asyncFn) {
  if (!button) return asyncFn();
  const label = button.textContent;
  const minW  = button.offsetWidth;
  button.style.minWidth = minW + "px";
  button.disabled = true;
  button.innerHTML = `<span class="btn-spinner"></span>`;
  try {
    return await asyncFn();
  } finally {
    button.disabled = false;
    button.textContent = label;
    button.style.minWidth = "";
  }
}

// =====================================================================
// Render pipeline
// =====================================================================

// For query commands — renders table silently on success; toasts on 0 results or error.
function renderQueryResult(lines) {
  if (!lines || !lines.length) return;
  const first = lines[0];

  if (first === "RESULT_BEGIN") {
    const rows = lines.filter(l => l.startsWith("RESULT|"));
    clearTable();
    if (!rows.length) {
      showToast("No courses found.", "warning");
      return;
    }
    const tbody = document.getElementById("result-body");
    rows.forEach(line => {
      const fields = line.split("|").slice(1);
      const tr = document.createElement("tr");
      fields.slice(0, 9).forEach(val => {
        const td = document.createElement("td");
        td.textContent = val.trim();
        tr.appendChild(td);
      });
      tbody.appendChild(tr);
    });
    document.getElementById("results-card").classList.add("has-rows");
    const noun = rows.length === 1 ? "course" : "courses";
    document.getElementById("summary-bar").textContent = `${rows.length} ${noun}`;
    return;
  }

  if (first.startsWith("RESULT_NONE|")) {
    clearTable();
    showToast("No courses found.", "warning");
    return;
  }

  if (first.startsWith("FAILURE|") || first.startsWith("ERROR|")) {
    const parts = first.split("|");
    const code  = parts[1] || "";
    const msg   = parts.slice(2).join("|");
    showToast(friendlyError(code) + (msg ? ": " + msg : ""), "error");
    return;
  }

  showToast(first, "info");
}

// For write commands — caller supplies the contextual success message.
function renderWriteResult(lines, successMsg) {
  if (!lines || !lines.length) return;
  const first = lines[0];

  if (first.startsWith("OK|")) {
    showToast(successMsg, "success");
    return;
  }

  if (first.startsWith("FAILURE|") || first.startsWith("ERROR|")) {
    const parts = first.split("|");
    const code  = parts[1] || "";
    const msg   = parts.slice(2).join("|");
    showToast(friendlyError(code) + (msg ? ": " + msg : ""), "error");
    return;
  }

  showToast(first, "info");
}

// =====================================================================
// Bridge fetch wrapper
// =====================================================================

async function call(cmd) {
  try {
    const resp = await fetch(`${BRIDGE}/api/command`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ session_id: sessionId, command: cmd }),
    });
    const data = await resp.json();
    if (!data.ok) {
      showToast(data.error || "Bridge error", "error");
      return [];
    }
    return data.lines || [];
  } catch (err) {
    showToast("Network error: " + err.message, "error");
    return [];
  }
}

// =====================================================================
// Tab switching — clears stale toasts on every switch
// =====================================================================

function switchTab(name) {
  clearAllToasts();
  document.querySelectorAll(".tab-btn").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.tab === name);
  });
  document.querySelectorAll(".tab-panel").forEach(panel => {
    panel.classList.toggle("active", panel.id === "tab-" + name);
  });
}

// =====================================================================
// Login modal
// =====================================================================

function openLoginModal() {
  lastFocusedTrigger = document.activeElement;
  const modal = document.getElementById("login-modal");
  modal.classList.add("open");
  modal.setAttribute("aria-hidden", "false");
  setTimeout(() => {
    const input = document.getElementById("username");
    if (input) input.focus();
  }, 50);
}

function closeLoginModal() {
  const modal = document.getElementById("login-modal");
  modal.classList.remove("open");
  modal.setAttribute("aria-hidden", "true");
  if (lastFocusedTrigger && typeof lastFocusedTrigger.focus === "function") {
    lastFocusedTrigger.focus();
  }
  lastFocusedTrigger = null;
}

function onModalOverlayClick(event) {
  if (event.target.id === "login-modal") closeLoginModal();
}

// =====================================================================
// Auth
// =====================================================================

async function login(event) {
  if (event) event.preventDefault();
  const user = document.getElementById("username").value.trim();
  const pass = document.getElementById("password").value;
  if (!user || !pass) {
    showToast("Please enter username and password.", "warning");
    return;
  }
  const submitBtn = document.querySelector("#login-form button[type='submit']");
  await withLoading(submitBtn, async () => {
    const lines = await call(`LOGIN|${user}|${pass}`);
    if (!lines.length) return;
    const first = lines[0];
    if (first.startsWith("SUCCESS|")) {
      const newRole = first.split("|")[1] || "student";
      setRole(newRole);
      closeLoginModal();
      document.getElementById("username").value = "";
      document.getElementById("password").value = "";
      switchTab("query"); // clears any lingering toasts
      showToast(`Signed in as ${user} (${newRole}).`, "success");
    } else {
      const parts = first.split("|");
      const code  = parts[1] || "";
      const msg   = parts.slice(2).join("|");
      showToast(friendlyError(code) + (msg ? ": " + msg : ""), "error");
    }
  });
}

async function logout() {
  await call("LOGOUT");
  setRole("guest");
  clearTable();
  switchTab("query"); // clears any lingering toasts
  showToast("Logged out successfully.", "info");
}

// =====================================================================
// Query commands — btn passed via onclick="doXxx(this)"
// =====================================================================

async function doQuery(btn) {
  const code = document.getElementById("q-code").value.trim();
  if (!code) { showToast("Please enter a course code.", "warning"); return; }
  await withLoading(btn, async () => renderQueryResult(await call(`QUERY|${code}`)));
}

async function doSearchInstructor(btn) {
  const name = document.getElementById("q-instructor").value.trim();
  if (!name) { showToast("Please enter an instructor name.", "warning"); return; }
  await withLoading(btn, async () => renderQueryResult(await call(`SEARCH_INSTRUCTOR|${name}`)));
}

async function doSearchTime(btn) {
  const day  = document.getElementById("q-day").value;
  const time = document.getElementById("q-time").value.trim();
  if (!time) { showToast("Please enter a time (HH:MM).", "warning"); return; }
  await withLoading(btn, async () => renderQueryResult(await call(`SEARCH_TIME|${day}|${time}`)));
}

async function doListAll(btn) {
  const sem = document.getElementById("q-semester").value.trim();
  await withLoading(btn, async () =>
    renderQueryResult(await call(sem ? `LIST_ALL|${sem}` : "LIST_ALL")));
}

// =====================================================================
// Admin commands — btn passed via onclick="doXxx(this)"
// =====================================================================

function fieldVal(id) { return document.getElementById(id).value.trim(); }

async function doAdd(btn) {
  const ids    = ["a-code","a-title","a-section","a-instructor",
                  "a-day","a-time","a-duration","a-classroom","a-semester"];
  const values = ids.map(fieldVal);
  if (values.some(v => !v)) {
    showToast("All 9 fields are required for ADD.", "warning");
    return;
  }
  const [code, , section] = values;
  await withLoading(btn, async () => {
    renderWriteResult(
      await call("ADD|" + values.join("|")),
      `Course ${code} (${section}) added successfully.`
    );
  });
}

async function doUpdate(btn) {
  const code    = fieldVal("u-code");
  const section = fieldVal("u-section");
  const field   = fieldVal("u-field");
  const newVal  = fieldVal("u-value");
  if (!code || !section || !newVal) {
    showToast("Code, section and new value are required.", "warning");
    return;
  }
  await withLoading(btn, async () => {
    renderWriteResult(
      await call(`UPDATE|${code}|${section}|${field}|${newVal}`),
      `Updated ${code} (${section}): ${field} → ${newVal}`
    );
  });
}

async function doDelete(btn) {
  const code    = fieldVal("d-code");
  const section = fieldVal("d-section");
  if (!code || !section) {
    showToast("Course code and section are required.", "warning");
    return;
  }
  await withLoading(btn, async () => {
    renderWriteResult(
      await call(`DELETE|${code}|${section}`),
      `Course ${code} (${section}) deleted.`
    );
  });
}

// =====================================================================
// Init
// =====================================================================

document.addEventListener("DOMContentLoaded", async () => {
  try {
    const resp = await fetch(`${BRIDGE}/api/connect`, { method: "POST" });
    const data = await resp.json();
    if (data.ok) {
      sessionId = data.session_id;
      const lines = data.lines || [];
      if (lines.length && lines[0].startsWith("WELCOME|")) {
        const parts = lines[0].split("|");
        showToast(parts.slice(1).join(" — "), "info");
      }
    } else {
      showToast("Cannot connect to server: " + (data.error || "unknown"), "error");
    }
  } catch (_) {
    showToast("Cannot reach bridge — is bridge.py running on port 50002?", "error");
  }

  // Enter key in any inline input-row fires the adjacent button.
  document.querySelectorAll(".input-row input").forEach(input => {
    input.addEventListener("keydown", e => {
      if (e.key !== "Enter") return;
      input.closest(".input-row")?.querySelector("button")?.click();
    });
  });
});

// Global ESC handler — closes the login modal when it is open.
document.addEventListener("keydown", (e) => {
  if (e.key !== "Escape") return;
  const modal = document.getElementById("login-modal");
  if (modal && modal.classList.contains("open")) closeLoginModal();
});

// Graceful TCP disconnect when the tab closes.
window.addEventListener("beforeunload", () => {
  if (!sessionId) return;
  navigator.sendBeacon(
    `${BRIDGE}/api/disconnect`,
    new URLSearchParams({ session_id: sessionId })
  );
});
