"""
Course Timetable Inquiry System — GUI Client
Requires: pip install customtkinter
Connects to the C++ server at 127.0.0.1:8888
"""

import socket
import threading
import tkinter as tk
import tkinter.messagebox as msgbox
import tkinter.font as tkfont
from tkinter import ttk
import customtkinter as ctk
from datetime import datetime

# ─── Theme ─────────────────────────────────────────────────────────────────────
ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

# Palette
BG_DARK   = "#0D1117"
BG_CARD   = "#161B22"
BG_INPUT  = "#21262D"
BG_HOVER  = "#1C2128"
ACCENT    = "#58A6FF"
ACCENT2   = "#3FB950"
WARN      = "#F78166"
MUTED     = "#8B949E"
TEXT      = "#E6EDF3"
TEXT_DIM  = "#C9D1D9"
BORDER    = "#30363D"
GOLD      = "#E3B341"

# ─── Network ───────────────────────────────────────────────────────────────────
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8888
BUFFER_SIZE  = 8192


class TimetableClient:
    """Low-level socket client — runs receives in a background thread."""

    def __init__(self):
        self.sock = None
        self._lock = threading.Lock()

    def connect(self, host, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(8)
        self.sock.connect((host, int(port)))
        return self._recv_until_done()

    def send(self, line):
        with self._lock:
            self.sock.sendall((line + "\n").encode())
            return self._recv_until_done()

    def _recv_until_done(self):
        data = b""
        TERMINALS = [
            b"RESULT END\n", b"RESULT NONE", b"SUCCESS", b"FAILURE",
            b"ERROR", b"OK ", b"BYE", b"WELCOME",
        ]
        while True:
            chunk = self.sock.recv(BUFFER_SIZE)
            if not chunk:
                break
            data += chunk
            # HELP response ends with QUIT line
            if data.startswith(b"HELP") and b"QUIT" in data:
                break
            elif any(t in data for t in TERMINALS):
                break
        return data.decode(errors="replace")

    def close(self):
        if self.sock:
            try:
                self.sock.sendall(b"QUIT\n")
            except Exception:
                pass
            self.sock.close()
            self.sock = None


# ─── Widgets ───────────────────────────────────────────────────────────────────

class SidebarButton(ctk.CTkButton):
    def __init__(self, parent, icon, label, command, **kw):
        super().__init__(
            parent,
            text=f"  {icon}  {label}",
            anchor="w",
            height=44,
            corner_radius=8,
            fg_color="transparent",
            hover_color=BG_HOVER,
            text_color=TEXT_DIM,
            font=ctk.CTkFont("Segoe UI", 13),
            command=command,
            **kw,
        )

    def set_active(self, active: bool):
        if active:
            self.configure(fg_color=BG_INPUT, text_color=ACCENT,
                           font=ctk.CTkFont("Segoe UI", 13, weight="bold"))
        else:
            self.configure(fg_color="transparent", text_color=TEXT_DIM,
                           font=ctk.CTkFont("Segoe UI", 13))


class ResultsBox(ctk.CTkTextbox):
    """Styled read-only textbox for server output."""

    def __init__(self, parent, **kw):
        super().__init__(
            parent,
            fg_color=BG_DARK,
            border_color=BORDER,
            border_width=1,
            text_color=TEXT,
            font=ctk.CTkFont("Consolas", 12),
            wrap="word",
            state="disabled",
            **kw,
        )
        # Tag colors
        self._textbox.tag_config("success", foreground=ACCENT2)
        self._textbox.tag_config("error",   foreground=WARN)
        self._textbox.tag_config("accent",  foreground=ACCENT)
        self._textbox.tag_config("muted",   foreground=MUTED)
        self._textbox.tag_config("gold",    foreground=GOLD)
        self._textbox.tag_config("header",  foreground=ACCENT,
                                 font=("Consolas", 12, "bold"))

    def clear(self):
        self.configure(state="normal")
        self.delete("1.0", "end")
        self.configure(state="disabled")

    def append(self, text, tag=None):
        self.configure(state="normal")
        if tag:
            self._textbox.insert("end", text, tag)
        else:
            self.insert("end", text)
        self.configure(state="disabled")
        self.see("end")

    def render_response(self, raw: str):
        self.clear()
        ts = datetime.now().strftime("%H:%M:%S")
        self.append(f"[{ts}] ", "muted")
        for line in raw.splitlines():
            if not line:
                continue
            if line.startswith("WELCOME"):
                self.append(line + "\n", "accent")
            elif line.startswith("SUCCESS"):
                self.append("✓ " + line[8:] + "\n", "success")
            elif line.startswith("FAILURE") or line.startswith("ERROR"):
                self.append("✗ " + line + "\n", "error")
            elif line.startswith("OK "):
                self.append("✓ " + line[3:] + "\n", "success")
            elif line == "RESULT BEGIN":
                self.append("─" * 60 + "\n", "muted")
            elif line == "RESULT END":
                self.append("─" * 60 + "\n", "muted")
            elif line.startswith("RESULT NONE"):
                self.append("⚠  " + line[12:] + "\n", "gold")
            elif line.startswith("RESULT "):
                self.append("  " + line[7:] + "\n")
            elif line.startswith("BYE"):
                self.append(line + "\n", "muted")
            elif line.startswith("HELP"):
                self.append(line + "\n", "accent")
            else:
                self.append(line + "\n")


# ─── Login Dialog ──────────────────────────────────────────────────────────────

class LoginDialog(ctk.CTkToplevel):
    def __init__(self, parent, callback):
        super().__init__(parent)
        self.callback = callback
        self.title("Login")
        self.geometry("360x280")
        self.resizable(False, False)
        self.configure(fg_color=BG_CARD)
        self.grab_set()
        self.lift()
        self.focus_force()

        ctk.CTkLabel(self, text="🔐  Sign In", font=ctk.CTkFont("Segoe UI", 18, "bold"),
                     text_color=TEXT).pack(pady=(28, 4))
        ctk.CTkLabel(self, text="Enter your credentials", font=ctk.CTkFont("Segoe UI", 12),
                     text_color=MUTED).pack(pady=(0, 20))

        self._user = ctk.CTkEntry(self, placeholder_text="Username", height=38,
                                  fg_color=BG_INPUT, border_color=BORDER,
                                  text_color=TEXT, width=260)
        self._user.pack(pady=4)
        self._pass = ctk.CTkEntry(self, placeholder_text="Password", show="•", height=38,
                                  fg_color=BG_INPUT, border_color=BORDER,
                                  text_color=TEXT, width=260)
        self._pass.pack(pady=4)
        self._pass.bind("<Return>", lambda e: self._submit())

        ctk.CTkButton(self, text="Login", height=38, width=260,
                      fg_color=ACCENT, hover_color="#4090EE",
                      font=ctk.CTkFont("Segoe UI", 13, "bold"),
                      command=self._submit).pack(pady=(16, 0))

    def _submit(self):
        u, p = self._user.get().strip(), self._pass.get().strip()
        if u and p:
            self.destroy()
            self.callback(u, p)


# ─── Connection Dialog ─────────────────────────────────────────────────────────

class ConnectDialog(ctk.CTkToplevel):
    def __init__(self, parent, callback):
        super().__init__(parent)
        self.callback = callback
        self.title("Connect to Server")
        self.geometry("360x240")
        self.resizable(False, False)
        self.configure(fg_color=BG_CARD)
        self.grab_set()
        self.lift()
        self.focus_force()

        ctk.CTkLabel(self, text="🌐  Server Connection",
                     font=ctk.CTkFont("Segoe UI", 18, "bold"),
                     text_color=TEXT).pack(pady=(28, 4))
        ctk.CTkLabel(self, text="Host and port to connect",
                     font=ctk.CTkFont("Segoe UI", 12),
                     text_color=MUTED).pack(pady=(0, 20))

        self._host = ctk.CTkEntry(self, placeholder_text="Host (127.0.0.1)", height=38,
                                  fg_color=BG_INPUT, border_color=BORDER,
                                  text_color=TEXT, width=260)
        self._host.insert(0, DEFAULT_HOST)
        self._host.pack(pady=4)
        self._port = ctk.CTkEntry(self, placeholder_text="Port (8888)", height=38,
                                  fg_color=BG_INPUT, border_color=BORDER,
                                  text_color=TEXT, width=260)
        self._port.insert(0, str(DEFAULT_PORT))
        self._port.pack(pady=4)

        ctk.CTkButton(self, text="Connect", height=38, width=260,
                      fg_color=ACCENT2, hover_color="#35A346",
                      font=ctk.CTkFont("Segoe UI", 13, "bold"),
                      command=self._submit).pack(pady=(16, 0))

    def _submit(self):
        h, p = self._host.get().strip(), self._port.get().strip()
        if h and p:
            self.destroy()
            self.callback(h, p)


# ─── Add Course Dialog ─────────────────────────────────────────────────────────

class AddCourseDialog(ctk.CTkToplevel):
    FIELDS = [
        ("Course Code",  "e.g. COMP3003"),
        ("Title",        "e.g. Data Communications"),
        ("Section",      "e.g. S1"),
        ("Instructor",   "e.g. Dr. Chan"),
        ("Day",          "Mon / Tue / Wed / Thu / Fri"),
        ("Time",         "HH:MM  e.g. 10:00"),
        ("Duration",     "e.g. 2h"),
        ("Classroom",    "e.g. A101"),
        ("Semester",     "e.g. 2026S1"),
    ]

    def __init__(self, parent, callback):
        super().__init__(parent)
        self.callback = callback
        self.title("Add Course")
        self.geometry("440x560")
        self.resizable(False, False)
        self.configure(fg_color=BG_CARD)
        self.grab_set()
        self.lift()
        self.focus_force()

        ctk.CTkLabel(self, text="➕  Add New Course",
                     font=ctk.CTkFont("Segoe UI", 16, "bold"),
                     text_color=TEXT).pack(pady=(20, 4))
        ctk.CTkLabel(self, text="All fields required",
                     font=ctk.CTkFont("Segoe UI", 11),
                     text_color=MUTED).pack(pady=(0, 12))

        frame = ctk.CTkFrame(self, fg_color="transparent")
        frame.pack(fill="both", expand=True, padx=24)

        self._entries = []
        for label, hint in self.FIELDS:
            ctk.CTkLabel(frame, text=label,
                         font=ctk.CTkFont("Segoe UI", 11),
                         text_color=MUTED, anchor="w").pack(fill="x", pady=(4, 0))
            e = ctk.CTkEntry(frame, placeholder_text=hint, height=34,
                             fg_color=BG_INPUT, border_color=BORDER,
                             text_color=TEXT)
            e.pack(fill="x", pady=(0, 2))
            self._entries.append(e)

        ctk.CTkButton(self, text="Add Course", height=38,
                      fg_color=ACCENT2, hover_color="#35A346",
                      font=ctk.CTkFont("Segoe UI", 13, "bold"),
                      command=self._submit).pack(pady=16, fill="x", padx=24)

    def _submit(self):
        vals = [e.get().strip() for e in self._entries]
        if all(vals):
            self.destroy()
            self.callback("|".join(vals))
        else:
            msgbox.showerror("Error", "All fields are required.", parent=self)


# ─── Main App ──────────────────────────────────────────────────────────────────

class App(ctk.CTk):
    NAV = [
        ("search", "🔍", "Query by Code"),
        ("instructor", "👤", "Search Instructor"),
        ("list", "📋", "List All Courses"),
        ("time", "🕐", "Search by Time"),
        ("add", "➕", "Add Course"),
        ("update", "✏️", "Update Course"),
        ("delete", "🗑", "Delete Course"),
        ("help", "❓", "Help"),
    ]
    ADMIN_PAGES = {"add", "update", "delete"}

    def __init__(self):
        super().__init__()
        self.title("Course Timetable Inquiry System")
        self.geometry("1100x700")
        self.minsize(900, 600)
        self.configure(fg_color=BG_DARK)

        self.client = TimetableClient()
        self.connected = False
        self.logged_in = False
        self.is_admin = False
        self.current_page = "search"
        self._sidebar_btns = {}

        self._build_ui()
        self._show_page("search")

    # ── UI Build ──────────────────────────────────────────────────────────────

    def _build_ui(self):
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        # ── Sidebar ──────────────────────────────────────────────────────────
        sidebar = ctk.CTkFrame(self, width=220, fg_color=BG_CARD,
                               corner_radius=0, border_width=0)
        sidebar.grid(row=0, column=0, sticky="nsw")
        sidebar.grid_propagate(False)
        sidebar.grid_rowconfigure(20, weight=1)

        # Logo
        logo_f = ctk.CTkFrame(sidebar, fg_color="transparent")
        logo_f.grid(row=0, column=0, sticky="ew", padx=16, pady=(20, 8))
        ctk.CTkLabel(logo_f, text="📅", font=ctk.CTkFont(size=26)).pack(side="left")
        ctk.CTkLabel(logo_f, text=" Timetable",
                     font=ctk.CTkFont("Segoe UI", 16, "bold"),
                     text_color=TEXT).pack(side="left")

        ctk.CTkFrame(sidebar, height=1, fg_color=BORDER).grid(
            row=1, column=0, sticky="ew", padx=12, pady=6)

        # Nav buttons
        for i, (page_id, icon, label) in enumerate(self.NAV):
            btn = SidebarButton(sidebar, icon, label,
                                command=lambda p=page_id: self._show_page(p))
            btn.grid(row=i + 2, column=0, sticky="ew", padx=8, pady=2)
            self._sidebar_btns[page_id] = btn

        # Divider above status
        ctk.CTkFrame(sidebar, height=1, fg_color=BORDER).grid(
            row=18, column=0, sticky="ew", padx=12, pady=6)

        # Connection status
        self._conn_label = ctk.CTkLabel(
            sidebar, text="⚫  Disconnected",
            font=ctk.CTkFont("Segoe UI", 11), text_color=MUTED,
            anchor="w")
        self._conn_label.grid(row=19, column=0, sticky="ew", padx=16, pady=2)

        self._user_label = ctk.CTkLabel(
            sidebar, text="",
            font=ctk.CTkFont("Segoe UI", 11), text_color=MUTED,
            anchor="w")
        self._user_label.grid(row=20, column=0, sticky="ew", padx=16, pady=2)

        # Action buttons
        self._connect_btn = ctk.CTkButton(
            sidebar, text="Connect", height=34,
            fg_color=ACCENT2, hover_color="#35A346",
            font=ctk.CTkFont("Segoe UI", 12, "bold"),
            command=self._open_connect)
        self._connect_btn.grid(row=21, column=0, sticky="ew", padx=12, pady=4)

        self._login_btn = ctk.CTkButton(
            sidebar, text="Login", height=34,
            fg_color=ACCENT, hover_color="#4090EE",
            font=ctk.CTkFont("Segoe UI", 12, "bold"),
            command=self._open_login, state="disabled")
        self._login_btn.grid(row=22, column=0, sticky="ew", padx=12, pady=(0, 20))

        # ── Main content ──────────────────────────────────────────────────────
        self._main = ctk.CTkFrame(self, fg_color=BG_DARK, corner_radius=0)
        self._main.grid(row=0, column=1, sticky="nsew")
        self._main.grid_rowconfigure(1, weight=1)
        self._main.grid_columnconfigure(0, weight=1)

        # Top bar
        topbar = ctk.CTkFrame(self._main, fg_color=BG_CARD, height=56,
                              corner_radius=0)
        topbar.grid(row=0, column=0, sticky="ew")
        topbar.grid_propagate(False)
        topbar.grid_columnconfigure(1, weight=1)

        self._page_title = ctk.CTkLabel(
            topbar, text="Query by Course Code",
            font=ctk.CTkFont("Segoe UI", 17, "bold"), text_color=TEXT, anchor="w")
        self._page_title.grid(row=0, column=0, padx=20, pady=12, sticky="w")

        self._status_dot = ctk.CTkLabel(
            topbar, text="⚫  Not connected",
            font=ctk.CTkFont("Segoe UI", 11), text_color=MUTED, anchor="e")
        self._status_dot.grid(row=0, column=1, padx=20, sticky="e")

        # Page container
        self._page_frame = ctk.CTkFrame(self._main, fg_color=BG_DARK,
                                        corner_radius=0)
        self._page_frame.grid(row=1, column=0, sticky="nsew", padx=0, pady=0)
        self._page_frame.grid_rowconfigure(0, weight=1)
        self._page_frame.grid_columnconfigure(0, weight=1)

        self._pages = {}
        self._build_pages()

    # ── Page builder ──────────────────────────────────────────────────────────

    def _build_pages(self):
        self._pages["search"]     = self._make_query_page()
        self._pages["instructor"] = self._make_instructor_page()
        self._pages["list"]       = self._make_list_page()
        self._pages["time"]       = self._make_time_page()
        self._pages["add"]        = self._make_add_page()
        self._pages["update"]     = self._make_update_page()
        self._pages["delete"]     = self._make_delete_page()
        self._pages["help"]       = self._make_help_page()

    def _make_card(self, parent):
        card = ctk.CTkFrame(parent, fg_color=BG_CARD, corner_radius=12,
                            border_width=1, border_color=BORDER)
        return card

    def _results_section(self, parent):
        lbl = ctk.CTkLabel(parent, text="Results",
                           font=ctk.CTkFont("Segoe UI", 12, "bold"),
                           text_color=MUTED, anchor="w")
        lbl.pack(fill="x", padx=20, pady=(0, 6))
        box = ResultsBox(parent)
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return box

    def _action_btn(self, parent, text, cmd, color=ACCENT):
        return ctk.CTkButton(
            parent, text=text, height=38, corner_radius=8,
            fg_color=color, hover_color="#4090EE" if color == ACCENT else "#35A346",
            font=ctk.CTkFont("Segoe UI", 13, "bold"),
            command=cmd)

    # ── Individual pages ──────────────────────────────────────────────────────

    def _make_query_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="x", padx=20, pady=20)
        ctk.CTkLabel(card, text="Course Code",
                     font=ctk.CTkFont("Segoe UI", 11), text_color=MUTED,
                     anchor="w").pack(fill="x", padx=20, pady=(16, 2))
        row = ctk.CTkFrame(card, fg_color="transparent")
        row.pack(fill="x", padx=20, pady=(0, 16))
        entry = ctk.CTkEntry(row, placeholder_text="e.g.  COMP3003", height=40,
                             fg_color=BG_INPUT, border_color=BORDER,
                             text_color=TEXT, font=ctk.CTkFont("Segoe UI", 13))
        entry.pack(side="left", fill="x", expand=True, padx=(0, 10))
        box = ResultsBox(f)
        def go():
            self._run("QUERY " + entry.get().strip().upper(), box)
        entry.bind("<Return>", lambda e: go())
        self._action_btn(row, "Search", go).pack(side="left")
        ctk.CTkLabel(f, text="Results",
                     font=ctk.CTkFont("Segoe UI", 12, "bold"),
                     text_color=MUTED, anchor="w").pack(fill="x", padx=20, pady=(0, 6))
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return f

    def _make_instructor_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="x", padx=20, pady=20)
        ctk.CTkLabel(card, text="Instructor Name",
                     font=ctk.CTkFont("Segoe UI", 11), text_color=MUTED,
                     anchor="w").pack(fill="x", padx=20, pady=(16, 2))
        row = ctk.CTkFrame(card, fg_color="transparent")
        row.pack(fill="x", padx=20, pady=(0, 16))
        entry = ctk.CTkEntry(row, placeholder_text="e.g.  Dr. Chan  (partial OK)", height=40,
                             fg_color=BG_INPUT, border_color=BORDER,
                             text_color=TEXT, font=ctk.CTkFont("Segoe UI", 13))
        entry.pack(side="left", fill="x", expand=True, padx=(0, 10))
        box = ResultsBox(f)
        def go():
            self._run("SEARCH_INSTRUCTOR " + entry.get().strip(), box)
        entry.bind("<Return>", lambda e: go())
        self._action_btn(row, "Search", go).pack(side="left")
        ctk.CTkLabel(f, text="Results",
                     font=ctk.CTkFont("Segoe UI", 12, "bold"),
                     text_color=MUTED, anchor="w").pack(fill="x", padx=20, pady=(0, 6))
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return f

    def _make_list_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="x", padx=20, pady=20)
        ctk.CTkLabel(card, text="Filter by Semester  (leave blank for all)",
                     font=ctk.CTkFont("Segoe UI", 11), text_color=MUTED,
                     anchor="w").pack(fill="x", padx=20, pady=(16, 2))
        row = ctk.CTkFrame(card, fg_color="transparent")
        row.pack(fill="x", padx=20, pady=(0, 16))
        entry = ctk.CTkEntry(row, placeholder_text="e.g.  2026S1", height=40,
                             fg_color=BG_INPUT, border_color=BORDER,
                             text_color=TEXT, font=ctk.CTkFont("Segoe UI", 13))
        entry.pack(side="left", fill="x", expand=True, padx=(0, 10))
        box = ResultsBox(f)
        def go():
            self._run("LIST_ALL " + entry.get().strip(), box)
        entry.bind("<Return>", lambda e: go())
        self._action_btn(row, "List All", go, ACCENT2).pack(side="left")
        ctk.CTkLabel(f, text="Results",
                     font=ctk.CTkFont("Segoe UI", 12, "bold"),
                     text_color=MUTED, anchor="w").pack(fill="x", padx=20, pady=(0, 6))
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return f

    def _make_time_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="x", padx=20, pady=20)
        row_labels = ctk.CTkFrame(card, fg_color="transparent")
        row_labels.pack(fill="x", padx=20, pady=(16, 2))
        ctk.CTkLabel(row_labels, text="Day", font=ctk.CTkFont("Segoe UI", 11),
                     text_color=MUTED, width=180, anchor="w").pack(side="left")
        ctk.CTkLabel(row_labels, text="Time", font=ctk.CTkFont("Segoe UI", 11),
                     text_color=MUTED, anchor="w").pack(side="left", padx=(10, 0))
        row = ctk.CTkFrame(card, fg_color="transparent")
        row.pack(fill="x", padx=20, pady=(0, 16))
        day_var = ctk.StringVar(value="Mon")
        day_menu = ctk.CTkOptionMenu(row, variable=day_var,
                                     values=["Mon", "Tue", "Wed", "Thu", "Fri"],
                                     width=160, height=40, fg_color=BG_INPUT,
                                     button_color=BORDER, dropdown_fg_color=BG_CARD,
                                     text_color=TEXT)
        day_menu.pack(side="left", padx=(0, 10))
        time_e = ctk.CTkEntry(row, placeholder_text="HH:MM  e.g. 10:00", height=40,
                              fg_color=BG_INPUT, border_color=BORDER,
                              text_color=TEXT, font=ctk.CTkFont("Segoe UI", 13),
                              width=180)
        time_e.pack(side="left", padx=(0, 10))
        box = ResultsBox(f)
        def go():
            self._run(f"SEARCH_TIME {day_var.get()} {time_e.get().strip()}", box)
        time_e.bind("<Return>", lambda e: go())
        self._action_btn(row, "Search", go).pack(side="left")
        ctk.CTkLabel(f, text="Results",
                     font=ctk.CTkFont("Segoe UI", 12, "bold"),
                     text_color=MUTED, anchor="w").pack(fill="x", padx=20, pady=(0, 6))
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return f

    def _make_add_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="x", padx=20, pady=20)
        ctk.CTkLabel(card, text="Add a new course  (Admin only)",
                     font=ctk.CTkFont("Segoe UI", 13, "bold"),
                     text_color=GOLD, anchor="w").pack(fill="x", padx=20, pady=(16, 6))
        FIELDS = [
            ("Course Code",  "COMP3003"),
            ("Title",        "Data Communications"),
            ("Section",      "S1"),
            ("Instructor",   "Dr. Chan"),
            ("Day",          "Mon"),
            ("Time (HH:MM)", "10:00"),
            ("Duration",     "2h"),
            ("Classroom",    "A101"),
            ("Semester",     "2026S1"),
        ]
        entries = {}
        grid = ctk.CTkFrame(card, fg_color="transparent")
        grid.pack(fill="x", padx=20, pady=(0, 16))
        for i, (lbl, hint) in enumerate(FIELDS):
            col = (i % 3) * 2
            row_idx = i // 3
            ctk.CTkLabel(grid, text=lbl, font=ctk.CTkFont("Segoe UI", 10),
                         text_color=MUTED, anchor="w").grid(
                row=row_idx * 2, column=col, sticky="w", pady=(8, 0), padx=(0 if col == 0 else 10, 0))
            e = ctk.CTkEntry(grid, placeholder_text=hint, height=34,
                             fg_color=BG_INPUT, border_color=BORDER,
                             text_color=TEXT, width=180)
            e.grid(row=row_idx * 2 + 1, column=col, sticky="ew",
                   pady=(0, 4), padx=(0 if col == 0 else 10, 0))
            entries[lbl] = e
        box = ResultsBox(f)
        def go():
            vals = [entries[lbl].get().strip() for lbl, _ in FIELDS]
            if not all(vals):
                msgbox.showwarning("Missing", "Please fill all fields.")
                return
            self._run("ADD " + "|".join(vals), box)
        self._action_btn(card, "Add Course", go, ACCENT2).pack(padx=20, pady=(0, 16), anchor="e")
        ctk.CTkLabel(f, text="Results",
                     font=ctk.CTkFont("Segoe UI", 12, "bold"),
                     text_color=MUTED, anchor="w").pack(fill="x", padx=20, pady=(0, 6))
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return f

    def _make_update_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="x", padx=20, pady=20)
        ctk.CTkLabel(card, text="Update Course Field  (Admin only)",
                     font=ctk.CTkFont("Segoe UI", 13, "bold"),
                     text_color=GOLD, anchor="w").pack(fill="x", padx=20, pady=(16, 6))

        row1 = ctk.CTkFrame(card, fg_color="transparent")
        row1.pack(fill="x", padx=20, pady=(0, 4))
        for lbl in ("Course Code", "Section"):
            ctk.CTkLabel(row1, text=lbl, font=ctk.CTkFont("Segoe UI", 10),
                         text_color=MUTED, anchor="w", width=180).pack(side="left", padx=(0, 10))
        row1_e = ctk.CTkFrame(card, fg_color="transparent")
        row1_e.pack(fill="x", padx=20, pady=(0, 8))
        code_e = ctk.CTkEntry(row1_e, placeholder_text="COMP3003", height=34,
                              fg_color=BG_INPUT, border_color=BORDER, text_color=TEXT, width=180)
        code_e.pack(side="left", padx=(0, 10))
        sec_e  = ctk.CTkEntry(row1_e, placeholder_text="S1", height=34,
                              fg_color=BG_INPUT, border_color=BORDER, text_color=TEXT, width=180)
        sec_e.pack(side="left")

        row2 = ctk.CTkFrame(card, fg_color="transparent")
        row2.pack(fill="x", padx=20, pady=(0, 4))
        for lbl in ("Field", "New Value"):
            ctk.CTkLabel(row2, text=lbl, font=ctk.CTkFont("Segoe UI", 10),
                         text_color=MUTED, anchor="w", width=180).pack(side="left", padx=(0, 10))
        row2_e = ctk.CTkFrame(card, fg_color="transparent")
        row2_e.pack(fill="x", padx=20, pady=(0, 12))
        FIELD_OPTS = ["TITLE","INSTRUCTOR","DAY","TIME","DURATION","CLASSROOM","SEMESTER"]
        field_var = ctk.StringVar(value="TIME")
        field_m = ctk.CTkOptionMenu(row2_e, variable=field_var, values=FIELD_OPTS,
                                    width=175, height=34, fg_color=BG_INPUT,
                                    button_color=BORDER, dropdown_fg_color=BG_CARD,
                                    text_color=TEXT)
        field_m.pack(side="left", padx=(0, 10))
        val_e = ctk.CTkEntry(row2_e, placeholder_text="New value", height=34,
                             fg_color=BG_INPUT, border_color=BORDER, text_color=TEXT, width=180)
        val_e.pack(side="left")

        box = ResultsBox(f)
        def go():
            cmd = f"UPDATE {code_e.get().strip()} {sec_e.get().strip()} {field_var.get()} {val_e.get().strip()}"
            self._run(cmd, box)
        self._action_btn(card, "Update", go, GOLD).pack(padx=20, pady=(0, 16), anchor="e")
        ctk.CTkLabel(f, text="Results",
                     font=ctk.CTkFont("Segoe UI", 12, "bold"),
                     text_color=MUTED, anchor="w").pack(fill="x", padx=20, pady=(0, 6))
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return f

    def _make_delete_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="x", padx=20, pady=20)
        ctk.CTkLabel(card, text="Delete Course  (Admin only)",
                     font=ctk.CTkFont("Segoe UI", 13, "bold"),
                     text_color=WARN, anchor="w").pack(fill="x", padx=20, pady=(16, 6))
        row_l = ctk.CTkFrame(card, fg_color="transparent")
        row_l.pack(fill="x", padx=20, pady=(0, 2))
        for lbl in ("Course Code", "Section"):
            ctk.CTkLabel(row_l, text=lbl, font=ctk.CTkFont("Segoe UI", 10),
                         text_color=MUTED, width=200, anchor="w").pack(side="left", padx=(0, 10))
        row_e = ctk.CTkFrame(card, fg_color="transparent")
        row_e.pack(fill="x", padx=20, pady=(0, 12))
        code_e = ctk.CTkEntry(row_e, placeholder_text="COMP3003", height=40,
                              fg_color=BG_INPUT, border_color=BORDER,
                              text_color=TEXT, width=200)
        code_e.pack(side="left", padx=(0, 10))
        sec_e  = ctk.CTkEntry(row_e, placeholder_text="S1", height=40,
                              fg_color=BG_INPUT, border_color=BORDER,
                              text_color=TEXT, width=200)
        sec_e.pack(side="left", padx=(0, 10))
        box = ResultsBox(f)
        def go():
            c, s = code_e.get().strip(), sec_e.get().strip()
            if msgbox.askyesno("Confirm Delete", f"Delete {c}/{s}? This cannot be undone."):
                self._run(f"DELETE {c} {s}", box)
        btn = ctk.CTkButton(row_e, text="Delete", height=40, corner_radius=8,
                            fg_color=WARN, hover_color="#D9584A",
                            font=ctk.CTkFont("Segoe UI", 13, "bold"),
                            command=go)
        btn.pack(side="left")
        ctk.CTkLabel(f, text="Results",
                     font=ctk.CTkFont("Segoe UI", 12, "bold"),
                     text_color=MUTED, anchor="w").pack(fill="x", padx=20, pady=(0, 6))
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        return f

    def _make_help_page(self):
        f = ctk.CTkFrame(self._page_frame, fg_color="transparent")
        card = self._make_card(f)
        card.pack(fill="both", expand=True, padx=20, pady=20)
        ctk.CTkLabel(card, text="Protocol Reference",
                     font=ctk.CTkFont("Segoe UI", 14, "bold"),
                     text_color=TEXT, anchor="w").pack(fill="x", padx=20, pady=(16, 8))
        box = ResultsBox(card)
        box.pack(fill="both", expand=True, padx=20, pady=(0, 16))
        HELP_TEXT = """Commands (Client → Server)
─────────────────────────────────────────────────────
  LOGIN <user> <pass>               Authenticate
  LOGOUT                            End session
  QUERY <code>                      Search by course code
  SEARCH_INSTRUCTOR <name>          Search by instructor
  SEARCH_TIME <day> <HH:MM>         Search by time slot
  LIST_ALL [semester]               List all / filter by semester
  ADD code|title|sec|instr|...      Add course (admin)
  UPDATE <code> <sec> <field> <val> Modify a field (admin)
  DELETE <code> <section>           Remove course (admin)
  HELP                              Show commands
  QUIT                              Disconnect

Server Responses
─────────────────────────────────────────────────────
  WELCOME …         Connection banner
  SUCCESS …         Operation OK
  FAILURE …         Auth or operation failed
  RESULT BEGIN      Start of multi-line result
  RESULT <data>     One result record
  RESULT END        End of results
  RESULT NONE …     No matching records
  OK …              Admin operation confirmed
  ERROR …           Bad request or unauthorized
  BYE               Disconnect acknowledged

Default Credentials
─────────────────────────────────────────────────────
  admin   / admin123   → Administrator (full access)
  student / stu123     → Student (query only)
  alice   / alice456   → Student
  bob     / bob789     → Student
"""
        box.configure(state="normal")
        box.insert("1.0", HELP_TEXT)
        box.configure(state="disabled")
        return f

    # ── Navigation ─────────────────────────────────────────────────────────────

    def _show_page(self, page_id):
        if page_id in self.ADMIN_PAGES and not self.is_admin:
            msgbox.showwarning("Admin Only",
                               "Please log in as administrator to access this feature.")
            return
        for pid, frame in self._pages.items():
            frame.grid_forget()
            frame.pack_forget()
        self._pages[page_id].pack(fill="both", expand=True)
        self.current_page = page_id

        for pid, btn in self._sidebar_btns.items():
            btn.set_active(pid == page_id)

        titles = {k: v for _, k, v in [(x[0], x[0], x[2]) for x in self.NAV]}
        nice = {n[0]: n[2] for n in self.NAV}
        self._page_title.configure(text=nice.get(page_id, page_id))

    # ── Connection ─────────────────────────────────────────────────────────────

    def _open_connect(self):
        if self.connected:
            if msgbox.askyesno("Disconnect", "Disconnect from server?"):
                self.client.close()
                self.connected = False
                self.logged_in = False
                self.is_admin  = False
                self._update_status()
                self._connect_btn.configure(text="Connect", fg_color=ACCENT2)
                self._login_btn.configure(text="Login", state="disabled")
        else:
            ConnectDialog(self, self._do_connect)

    def _do_connect(self, host, port):
        def task():
            try:
                resp = self.client.connect(host, port)
                self.connected = True
                self.after(0, lambda: self._on_connected(resp))
            except Exception as e:
                err = str(e)
                self.after(0, lambda: msgbox.showerror("Connection Error", err))
        threading.Thread(target=task, daemon=True).start()

    def _on_connected(self, resp):
        self._update_status()
        self._connect_btn.configure(text="Disconnect", fg_color=WARN)
        self._login_btn.configure(state="normal")
        # Show welcome in current page's results if it has one
        if hasattr(self._pages.get(self.current_page, None), "render_response"):
            pass

    # ── Login ──────────────────────────────────────────────────────────────────

    def _open_login(self):
        if self.logged_in:
            def task():
                try:
                    resp = self.client.send("LOGOUT")
                    self.logged_in = False
                    self.is_admin  = False
                    self.after(0, self._update_status)
                    self.after(0, lambda: self._login_btn.configure(text="Login"))
                except Exception as e:
                    self.after(0, lambda: msgbox.showerror("Error", str(e)))
            threading.Thread(target=task, daemon=True).start()
        else:
            LoginDialog(self, self._do_login)

    def _do_login(self, user, pwd):
        def task():
            try:
                resp = self.client.send(f"LOGIN {user} {pwd}")
                if "SUCCESS" in resp:
                    self.logged_in = True
                    self.is_admin  = "admin" in resp
                    self.after(0, self._update_status)
                    self.after(0, lambda: self._login_btn.configure(text="Logout"))
                else:
                    self.after(0, lambda: msgbox.showerror("Login Failed",
                                                            resp.strip()))
            except Exception as e:
                err = str(e)
                self.after(0, lambda: msgbox.showerror("Error", err))
        threading.Thread(target=task, daemon=True).start()

    # ── Command runner ─────────────────────────────────────────────────────────

    def _run(self, cmd, results_box: ResultsBox):
        if not self.connected:
            msgbox.showwarning("Not Connected", "Please connect to the server first.")
            return

        def task():
            try:
                resp = self.client.send(cmd)
                self.after(0, lambda: results_box.render_response(resp))
            except Exception as e:
                err = str(e)
                self.after(0, lambda: results_box.render_response(f"ERROR {err}"))
        threading.Thread(target=task, daemon=True).start()

    # ── Status update ──────────────────────────────────────────────────────────

    def _update_status(self):
        if self.connected:
            self._conn_label.configure(text="🟢  Connected", text_color=ACCENT2)
            self._status_dot.configure(text=f"🟢  Connected", text_color=ACCENT2)
        else:
            self._conn_label.configure(text="⚫  Disconnected", text_color=MUTED)
            self._status_dot.configure(text="⚫  Not connected", text_color=MUTED)

        if self.logged_in:
            role = "Admin 🛡" if self.is_admin else "Student"
            self._user_label.configure(text=f"👤  {role}", text_color=GOLD if self.is_admin else TEXT_DIM)
        else:
            self._user_label.configure(text="", text_color=MUTED)


# ─── Entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    app = App()
    app.mainloop()
