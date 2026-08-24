"""
STM32 Motor Control System — Python Monitor v2.0
=================================================
Data format: Boot : Mode:X,ADC:XXXX,Duty:XX.X,Pos:XX.XX,RPM:XX.XX
Mode: 0=FORWARD  1=REVERSE  2=STOP
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading
import socket
import time
import csv
import os
import sys
import json
from collections import deque

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np

# ════════════════════════════════════════════════════
#  CONFIG
# ════════════════════════════════════════════════════
DEFAULT_PORT    = 8000
PLOT_WINDOW     = 30
MAX_POINTS      = 2000
DATA_TIMEOUT    = 3.0
RECONNECT_TMO   = 8.0
REFRESH_MS      = 80        # GUI refresh ~12fps

# ════════════════════════════════════════════════════
#  PALETTE
# ════════════════════════════════════════════════════
C = {
    "bg"      : "#0B0D12",
    "surface" : "#13151C",
    "card"    : "#181B24",
    "border"  : "#252836",
    "cyan"    : "#00D4FF",
    "green"   : "#00F5A0",
    "orange"  : "#FF8C42",
    "red"     : "#FF4560",
    "yellow"  : "#FFD166",
    "pink"    : "#FF6B9D",
    "purple"  : "#A78BFA",
    "text"    : "#D0D5E0",
    "dim"     : "#555B6E",
    "white"   : "#FFFFFF",
}

# Chart colors per signal
CHART_CLR = {
    "adc" : C["cyan"],
    "duty": C["green"],
    "pos" : C["orange"],
    "rpm" : C["pink"],
}

# ════════════════════════════════════════════════════
#  DATA STORE  (thread-safe)
# ════════════════════════════════════════════════════
class DataStore:
    def __init__(self):
        self.lock          = threading.Lock()
        self.t_start       = None
        self.times         = deque(maxlen=MAX_POINTS)
        self.adc           = deque(maxlen=MAX_POINTS)
        self.duty          = deque(maxlen=MAX_POINTS)
        self.pos           = deque(maxlen=MAX_POINTS)
        self.rpm           = deque(maxlen=MAX_POINTS)
        self.mode          = 2
        self.packet_count  = 0
        self.rx_rate       = 0.0
        self.last_raw      = ""
        self.last_rx_time  = 0.0
        self.connected     = False
        self.data_ok       = False
        self.client_ip     = ""

    def reset_data(self):
        """Clear chart data only."""
        with self.lock:
            self.times.clear()
            self.adc.clear()
            self.duty.clear()
            self.pos.clear()
            self.rpm.clear()
            self.packet_count = 0
            self.rx_rate      = 0.0
            self.t_start      = None

    def snapshot(self):
        """Return a copy of current arrays (call inside lock)."""
        return (
            list(self.times), list(self.adc),
            list(self.duty),  list(self.pos), list(self.rpm),
        )

store = DataStore()

# ════════════════════════════════════════════════════
#  PACKET PARSER
# ════════════════════════════════════════════════════

# CRC8-CCITT Table (Polynomial 0x07) matching STM32
crc8_table = [
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
]

def crc8_calc(data_bytes):
    crc = 0x00
    for b in data_bytes:
        crc = crc8_table[crc ^ b]
    return crc

def parse_packet(raw: str):
    """
    Input : '{"m":0,"a":2048,"d":50.0,"p":-45.50,"r":120.75,"s":123,"c":"XX"}'
    Output: dict or None
    """
    raw = raw.strip()
    if not raw.startswith("{") or not raw.endswith("}"):
        return None
        
    try:
        # Check CRC if present
        if ',"c":"' in raw:
            base_part, c_part = raw.rsplit(',"c":"', 1)
            original_payload = base_part + "}"
            crc_received_str = c_part.replace('"}', '').strip()
            crc_received = int(crc_received_str, 16)
            
            crc_calculated = crc8_calc(original_payload.encode('ascii'))
            if crc_received != crc_calculated:
                # CRC Mismatch, return special dict to indicate error
                return None
                
        data = json.loads(raw)
        
        result = {
            "mode": int(data.get("m", 2)),
            "adc": float(data.get("a", 0)),
            "duty": float(data.get("d", 0.0)),
            "pos": float(data.get("p", 0.0)),
            "rpm": float(data.get("r", 0.0))
        }
        return result
    except Exception:
        return None

# ════════════════════════════════════════════════════
#  TCP SERVER THREAD
# ════════════════════════════════════════════════════
class TCPServerThread(threading.Thread):
    def __init__(self, app):
        super().__init__(daemon=True)
        self.app          = app
        self._stop        = threading.Event()
        self._manual_stop = threading.Event()  # set when user clicks Disconnect
        self._port        = DEFAULT_PORT

    def set_port(self, port):
        self._port = port

    def stop(self):
        self._stop.set()
        self._manual_stop.set()

    def disconnect(self):
        """Stop only the current cycle (don't restart)."""
        self._manual_stop.set()
        self._stop.set()

    def run(self):
        while not self._stop.is_set() and not self._manual_stop.is_set():
            self._serve_once()
            if not self._stop.is_set() and not self._manual_stop.is_set():
                time.sleep(1)

    def _serve_once(self):
        self.app.ui_set_status("WAITING FOR ESP-01...", C["yellow"])
        self.app.ui_set_dot("tcp",  False)
        self.app.ui_set_dot("data", False)
        with store.lock:
            store.connected = False
            store.data_ok   = False

        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            srv.bind(("0.0.0.0", self._port))
            srv.listen(1)
            srv.settimeout(1.0)
        except OSError as e:
            self.app.ui_set_status(f"PORT {self._port} ERROR: {e}", C["red"])
            srv.close()
            self.app.ui_update_conn_buttons(connected=False, listening=False)
            time.sleep(3)
            return

        conn = None
        while not self._stop.is_set() and not self._manual_stop.is_set():
            try:
                conn, addr = srv.accept()
                break
            except socket.timeout:
                continue

        srv.close()
        if conn is None:
            self.app.ui_set_status("DISCONNECTED  —  Click [Connect] to start listening", C["dim"])
            self.app.ui_update_conn_buttons(connected=False, listening=False)
            return

        # ---- Connected ----
        client_ip = addr[0]
        with store.lock:
            store.connected  = True
            store.client_ip  = client_ip
            store.last_rx_time = time.time()
        self.app.ui_set_status(
            f"ESP-01 CONNECTED  ●  {client_ip}  |  PORT {self._port}", C["green"])
        self.app.ui_set_dot("tcp", True)

        buf           = ""
        rate_packets  = 0
        rate_t        = time.time()
        last_pkt_t    = time.time()

        conn.settimeout(0.3)

        try:
            while not self._stop.is_set():
                # ---- Receive ----
                try:
                    chunk = conn.recv(2048).decode("utf-8", errors="ignore")
                    if not chunk:
                        break
                    buf += chunk
                except socket.timeout:
                    chunk = None

                # ---- Parse lines ----
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                        
                    now = time.time()
                    parsed = parse_packet(line)
                    
                    with store.lock:
                        if store.t_start is None:
                            store.t_start = now
                        t_rel = now - store.t_start

                        if parsed is not None:
                            store.times.append(t_rel)
                            store.adc.append(parsed["adc"])
                            store.duty.append(parsed["duty"])
                            store.pos.append(parsed["pos"])
                            store.rpm.append(parsed["rpm"])
                            store.mode         = parsed["mode"]
                            store.packet_count += 1
                            store.data_ok      = True
                            rate_packets      += 1
                            store.last_raw     = line
                        else:
                            # Log error if packet failed to parse
                            store.last_raw     = "ERROR: " + line
                            
                        store.last_rx_time = now

                    last_pkt_t = now
                    self.app.ui_set_dot("data", True)

                    # RX Rate
                    elapsed = now - rate_t
                    if elapsed >= 1.0:
                        with store.lock:
                            store.rx_rate = rate_packets / elapsed
                        rate_packets = 0
                        rate_t = now

                # ---- Watchdogs ----
                idle = time.time() - last_pkt_t
                if store.data_ok and idle > DATA_TIMEOUT:
                    with store.lock:
                        store.data_ok = False
                    self.app.ui_set_dot("data", False)
                    self.app.ui_set_status(
                        f"ESP-01 CONNECTED  ●  {client_ip}  |  NO DATA ({idle:.0f}s)",
                        C["orange"])

                if (idle > RECONNECT_TMO and store.packet_count > 0) or self._manual_stop.is_set():
                    break   # force reconnect / manual disconnect

        except Exception:
            pass
        finally:
            try:
                conn.close()
            except Exception:
                pass
            with store.lock:
                store.connected = False
                store.data_ok   = False
            self.app.ui_set_dot("tcp",  False)
            self.app.ui_set_dot("data", False)
            # If manually disconnected, show idle message; else let restart loop decide
            if self._manual_stop.is_set():
                self.app.ui_set_status(
                    "DISCONNECTED  —  Click [Connect] to start listening", C["dim"])
                self.app.ui_update_conn_buttons(connected=False, listening=False)
            else:
                self.app.ui_update_conn_buttons(connected=False, listening=True)

# ════════════════════════════════════════════════════
#  HELPER WIDGETS
# ════════════════════════════════════════════════════
def sep(parent, color=None, orient="h", padx=0, pady=4):
    color = color or C["border"]
    if orient == "h":
        tk.Frame(parent, bg=color, height=1).pack(fill="x", padx=padx, pady=pady)
    else:
        tk.Frame(parent, bg=color, width=1).pack(fill="y", padx=padx, pady=pady, side="left")

# ════════════════════════════════════════════════════
#  MAIN APPLICATION
# ════════════════════════════════════════════════════
class App:
    def __init__(self, root):
        self.root = root
        self.root.title("STM32 Motor Control Monitor")
        self.root.geometry("1380x820")
        self.root.minsize(1100, 700)
        self.root.configure(bg=C["bg"])

        self._server      : TCPServerThread = None
        self._port_var    = tk.IntVar(value=DEFAULT_PORT)
        self._pw_var      = tk.IntVar(value=PLOT_WINDOW)
        self._btn_connect = None
        self._btn_disc    = None

        self._build_ui()
        # Do NOT auto-start — user presses Connect manually
        self._status_lbl.config(
            text="⏵  Click [Connect] to start listening for ESP-01",
            fg=C["cyan"])
        self.ui_update_conn_buttons(connected=False, listening=False)
        self._schedule_update()

    # ──────────────────────────────────────────────
    #  BUILD UI
    # ──────────────────────────────────────────────
    def _build_ui(self):
        # ===== TOP BAR =====
        top = tk.Frame(self.root, bg=C["surface"], pady=0)
        top.pack(fill="x", side="top")

        # Left: title + subtitle
        tl = tk.Frame(top, bg=C["surface"])
        tl.pack(side="left", padx=16, pady=10)
        tk.Label(tl, text="STM32  MOTOR CONTROL", font=("Segoe UI", 18, "bold"),
                 fg=C["cyan"], bg=C["surface"]).pack(anchor="w")
        tk.Label(tl, text="Realtime TCP Monitor  |  ESP-01  |  FreeRTOS",
                 font=("Segoe UI", 9), fg=C["dim"], bg=C["surface"]).pack(anchor="w")

        # Right: controls
        ctrl = tk.Frame(top, bg=C["surface"])
        ctrl.pack(side="right", padx=16, pady=8)

        # Port
        tk.Label(ctrl, text="Port:", font=("Segoe UI", 9), fg=C["dim"],
                 bg=C["surface"]).grid(row=0, column=0, sticky="e", padx=(0,4))
        port_entry = tk.Entry(ctrl, textvariable=self._port_var, width=6,
                              bg=C["card"], fg=C["text"], insertbackground=C["text"],
                              relief="flat", font=("Consolas", 10), bd=0)
        port_entry.grid(row=0, column=1, padx=(0,8), ipady=3)

        # Window
        tk.Label(ctrl, text="Window(s):", font=("Segoe UI", 9), fg=C["dim"],
                 bg=C["surface"]).grid(row=0, column=2, sticky="e", padx=(0,4))
        pw_entry = tk.Entry(ctrl, textvariable=self._pw_var, width=5,
                            bg=C["card"], fg=C["text"], insertbackground=C["text"],
                            relief="flat", font=("Consolas", 10), bd=0)
        pw_entry.grid(row=0, column=3, padx=(0,10), ipady=3)

        # Buttons
        self._btn_clear = self._mk_btn(ctrl, "⟳  Clear Chart", self._clear_chart, C["purple"])
        self._btn_clear.grid(row=0, column=4, padx=4)
        self._btn_csv = self._mk_btn(ctrl, "⬇  Export CSV", self._export_csv, C["green"])
        self._btn_csv.grid(row=0, column=5, padx=4)
        self._btn_restart = self._mk_btn(ctrl, "↺  Reconnect", self._restart_server, C["yellow"])
        self._btn_restart.grid(row=0, column=6, padx=4)

        # Connect / Disconnect buttons
        self._btn_connect = self._mk_btn(ctrl, "⏵  Connect",    self._connect,    C["cyan"])
        self._btn_connect.grid(row=0, column=7, padx=4)
        self._btn_disc    = self._mk_btn(ctrl, "⏹  Disconnect", self._disconnect,  C["red"])
        self._btn_disc.grid(row=0, column=8, padx=4)

        # ===== STATUS BAR =====
        sb = tk.Frame(self.root, bg=C["card"], pady=0)
        sb.pack(fill="x")
        tk.Frame(sb, bg=C["cyan"], width=4).pack(side="left", fill="y")
        self._status_lbl = tk.Label(sb, text="INITIALIZING...",
                                    font=("Segoe UI", 10, "bold"),
                                    fg=C["yellow"], bg=C["card"], pady=6, padx=10)
        self._status_lbl.pack(side="left")

        # Right: indicators
        ind = tk.Frame(sb, bg=C["card"])
        ind.pack(side="right", padx=12)
        self._dot_tcp  = self._mk_dot(ind, "TCP")
        self._dot_data = self._mk_dot(ind, "DATA")

        # ===== VALUE CARDS =====
        cards_wrap = tk.Frame(self.root, bg=C["bg"])
        cards_wrap.pack(fill="x", padx=10, pady=(8, 4))

        card_defs = [
            ("MOTOR MODE",   "STOP",        C["red"],    "_mode_var",  "_mode_lbl"),
            ("ADC  (0-4095)","0",           C["cyan"],   "_adc_var",   None),
            ("PWM DUTY",     "0.0 %",       C["green"],  "_duty_var",  None),
            ("POSITION",     "0.00 °",      C["orange"], "_pos_var",   None),
            ("MOTOR SPEED",  "0.00 RPM",    C["pink"],   "_rpm_var",   None),
        ]
        for label, init, color, vattr, lattr in card_defs:
            var, lbl = self._mk_card(cards_wrap, label, init, color)
            setattr(self, vattr, var)
            if lattr:
                setattr(self, lattr, lbl)

        # ===== NETWORK INFO STRIP =====
        net = tk.Frame(self.root, bg=C["surface"])
        net.pack(fill="x", padx=10, pady=(0, 6))

        info_items = [
            ("_pkt_var",  "Packets: 0",         C["green"]),
            ("_rate_var", "RX: 0.0 pkt/s",       C["green"]),
            ("_rx_var",   "Last RX: --:--:--",   C["yellow"]),
            ("_ip_var",   "Client: ---",          C["cyan"]),
        ]
        for attr, init, color in info_items:
            v = tk.StringVar(value=init)
            setattr(self, attr, v)
            tk.Label(net, textvariable=v, font=("Consolas", 9),
                     fg=color, bg=C["surface"], padx=16).pack(side="left")

        # ===== CHARTS =====
        chart_frame = tk.Frame(self.root, bg=C["bg"])
        chart_frame.pack(fill="both", expand=True, padx=10, pady=(0, 4))

        self._fig = Figure(facecolor=C["bg"], figsize=(15, 4.5))
        self._fig.subplots_adjust(left=0.055, right=0.985,
                                   top=0.88, bottom=0.14,
                                   wspace=0.30, hspace=0.55)
        self._axes  = {}
        self._lines = {}
        self._fills = {}

        chart_defs = [
            ("adc",  2, 1, "ADC INPUT",       "ADC Value",    [0, 4095]),
            ("duty", 2, 2, "PWM DUTY CYCLE",  "Duty (%)",     [0, 100]),
            ("pos",  2, 3, "MOTOR POSITION",  "Position (°)", None),
            ("rpm",  2, 4, "MOTOR SPEED",     "Speed (RPM)",  None),
        ]
        for key, rows, col, title, ylabel, ylim in chart_defs:
            ax = self._fig.add_subplot(1, 4, col, facecolor=C["card"])
            color = CHART_CLR[key]

            line,  = ax.plot([], [], color=color, linewidth=1.8, zorder=3)
            fill   = ax.fill_between([], [], alpha=0.12, color=color, zorder=2)

            ax.set_title(title, color=C["text"], fontsize=8.5,
                         fontweight="bold", pad=5)
            ax.set_xlabel("Time (s)", color=C["dim"], fontsize=7.5, labelpad=3)
            ax.set_ylabel(ylabel,     color=C["dim"], fontsize=7.5, labelpad=3)
            ax.tick_params(colors=C["dim"], labelsize=7, length=3)
            ax.grid(True, color=C["border"], linewidth=0.6, linestyle="--", alpha=0.7)
            for sp in ax.spines.values():
                sp.set_edgecolor(C["border"])
            if ylim:
                ax.set_ylim(ylim)
            if key == "rpm":
                ax.axhline(0, color=C["dim"], linewidth=0.7, linestyle="--", zorder=1)

            self._axes[key]  = ax
            self._lines[key] = line
            self._fills[key] = fill

        canvas = FigureCanvasTkAgg(self._fig, master=chart_frame)
        canvas.draw()
        canvas.get_tk_widget().configure(bg=C["bg"], highlightthickness=0)
        canvas.get_tk_widget().pack(fill="both", expand=True)
        self._canvas = canvas

        # ===== RAW DATA FOOTER =====
        foot = tk.Frame(self.root, bg="#08090D")
        foot.pack(fill="x", side="bottom")
        tk.Frame(foot, bg=C["dim"], height=1).pack(fill="x")
        self._raw_var = tk.StringVar(value="RAW ▸  waiting for data...")
        tk.Label(foot, textvariable=self._raw_var,
                 font=("Consolas", 8), fg=C["dim"], bg="#08090D",
                 anchor="w", pady=3).pack(fill="x", padx=10)

    # ──────────────────────────────────────────────
    #  WIDGET HELPERS
    # ──────────────────────────────────────────────
    def _mk_btn(self, parent, text, cmd, color):
        btn = tk.Label(parent, text=text, font=("Segoe UI", 9, "bold"),
                       fg=C["bg"], bg=color, padx=10, pady=4, cursor="hand2")
        btn.bind("<Button-1>", lambda e: cmd())
        btn.bind("<Enter>",    lambda e: btn.config(bg=self._lighten(color)))
        btn.bind("<Leave>",    lambda e: btn.config(bg=color))
        return btn

    @staticmethod
    def _lighten(hex_color, amt=30):
        r = int(hex_color[1:3], 16)
        g = int(hex_color[3:5], 16)
        b = int(hex_color[5:7], 16)
        r = min(255, r + amt)
        g = min(255, g + amt)
        b = min(255, b + amt)
        return f"#{r:02X}{g:02X}{b:02X}"

    def _mk_dot(self, parent, label):
        f = tk.Frame(parent, bg=C["card"])
        f.pack(side="left", padx=6)
        dot = tk.Label(f, text="●", font=("Segoe UI", 11),
                       fg=C["red"], bg=C["card"])
        dot.pack(side="left", padx=(0, 3))
        tk.Label(f, text=label, font=("Segoe UI", 8),
                 fg=C["dim"], bg=C["card"]).pack(side="left")
        return dot

    def _mk_card(self, parent, label, init, color):
        frame = tk.Frame(parent, bg=C["card"], padx=14, pady=10,
                         highlightthickness=1, highlightbackground=C["border"])
        frame.pack(side="left", expand=True, fill="both", padx=5)
        tk.Label(frame, text=label, font=("Segoe UI", 8),
                 fg=C["dim"], bg=C["card"]).pack(anchor="w")
        tk.Frame(frame, bg=color, height=2).pack(fill="x", pady=(2, 6))
        var = tk.StringVar(value=init)
        lbl = tk.Label(frame, textvariable=var,
                       font=("Segoe UI", 21, "bold"),
                       fg=color, bg=C["card"], anchor="w")
        lbl.pack(anchor="w")
        return var, lbl

    # ──────────────────────────────────────────────
    #  SERVER
    # ──────────────────────────────────────────────
    def _start_server(self):
        self._server = TCPServerThread(self)
        self._server.set_port(self._port_var.get())
        self._server.start()
        self.ui_update_conn_buttons(connected=False, listening=True)

    def _restart_server(self):
        if self._server:
            self._server.stop()
        store.reset_data()
        time.sleep(0.3)
        self._start_server()

    # ---- Connect / Disconnect ----
    def _connect(self):
        """Start listening for a new connection."""
        if self._server and self._server.is_alive():
            return  # already running
        # Immediate visual feedback
        self._flash_btn(self._btn_connect, C["cyan"])
        self._status_lbl.config(
            text=f"⏵  LISTENING ON PORT {self._port_var.get()}  —  Waiting for ESP-01...",
            fg=C["cyan"])
        store.reset_data()
        self._start_server()

    def _disconnect(self):
        """Stop the TCP server / drop the current client."""
        # Immediate visual feedback
        self._flash_btn(self._btn_disc, C["red"])
        self._status_lbl.config(
            text="⏹  DISCONNECTING...",
            fg=C["red"])
        if self._server:
            self._server.disconnect()
        with store.lock:
            store.connected = False
            store.data_ok   = False

    def _flash_btn(self, btn, color, duration_ms=120):
        """Briefly brighten a button to confirm click."""
        bright = self._lighten(color, 60)
        btn.config(bg=bright)
        self.root.after(duration_ms, lambda: btn.config(bg=color))

    def ui_update_conn_buttons(self, connected: bool, listening: bool):
        """Enable/disable Connect and Disconnect buttons (thread-safe)."""
        def _do():
            if self._btn_connect is None or self._btn_disc is None:
                return
            if listening or connected:
                # Server is active → only Disconnect makes sense
                self._btn_connect.config(bg=C["dim"],  fg=C["bg"],  cursor="")
                self._btn_disc.config(   bg=C["red"],  fg=C["bg"],  cursor="hand2")
            else:
                # Server is idle → only Connect makes sense
                self._btn_connect.config(bg=C["cyan"], fg=C["bg"],  cursor="hand2")
                self._btn_disc.config(   bg=C["dim"],  fg=C["bg"],  cursor="")
        self.root.after(0, _do)

    # ──────────────────────────────────────────────
    #  PERIODIC REFRESH
    # ──────────────────────────────────────────────
    def _schedule_update(self):
        self._update()
        self.root.after(REFRESH_MS, self._schedule_update)

    def _update(self):
        with store.lock:
            if not store.times:
                return
            t_arr, adc_arr, duty_arr, pos_arr, rpm_arr = store.snapshot()
            mode     = store.mode
            pkt      = store.packet_count
            rate     = store.rx_rate
            raw      = store.last_raw
            lrx      = store.last_rx_time
            cip      = store.client_ip
            data_ok  = store.data_ok

        if not t_arr:
            return

        pw   = max(10, self._pw_var.get())
        t_now = t_arr[-1]
        xmin = max(0.0, t_now - pw)
        xmax = max(float(pw), t_now)

        # ---- Update charts ----
        arr_map = {"adc": adc_arr, "duty": duty_arr, "pos": pos_arr, "rpm": rpm_arr}
        for key, line in self._lines.items():
            ya = arr_map[key]
            line.set_data(t_arr, ya)

            # Redraw fill
            self._fills[key].remove()
            ax   = self._axes[key]
            color= CHART_CLR[key]
            self._fills[key] = ax.fill_between(
                t_arr, ya, alpha=0.10, color=color, zorder=2)

        for ax in self._axes.values():
            ax.set_xlim(xmin, xmax)

        # Auto-scale pos & rpm
        if pos_arr:
            p = max(abs(v) for v in pos_arr[-200:])
            r = max(10.0, p * 1.25)
            self._axes["pos"].set_ylim(-r, r)
        if rpm_arr:
            r = max(abs(v) for v in rpm_arr[-200:])
            rng = max(20.0, r * 1.30)
            self._axes["rpm"].set_ylim(-rng, rng)

        self._canvas.draw_idle()

        # ---- Value cards ----
        mode_cfg = {
            0: ("▲  FORWARD", C["green"]),
            1: ("▼  REVERSE", C["orange"]),
            2: ("■  STOP",    C["red"]),
        }
        m_txt, m_clr = mode_cfg.get(mode, ("?  UNKNOWN", C["dim"]))
        self._mode_var.set(m_txt)
        self._mode_lbl.config(fg=m_clr)

        self._adc_var.set(f"{int(adc_arr[-1])}")
        self._duty_var.set(f"{duty_arr[-1]:.1f} %")
        self._pos_var.set(f"{pos_arr[-1]:.2f} °")
        self._rpm_var.set(f"{rpm_arr[-1]:.2f} RPM")

        # ---- Network strip ----
        self._pkt_var.set(f"Packets: {pkt}")
        self._rate_var.set(f"RX: {rate:.1f} pkt/s")
        if lrx > 0:
            self._rx_var.set("Last RX: " + time.strftime("%H:%M:%S", time.localtime(lrx)))
        if cip:
            self._ip_var.set(f"Client: {cip}")

        # ---- Status update ----
        if store.connected and data_ok and pkt > 0:
            self.ui_set_status(
                f"ESP-01 CONNECTED  ●  {cip}  |  PORT {self._port_var.get()}"
                f"  |  {rate:.1f} pkt/s  |  {pkt} packets received",
                C["green"])

        # ---- Raw footer ----
        if raw:
            self._raw_var.set("RAW ▸  " + raw)

    # ──────────────────────────────────────────────
    #  THREAD-SAFE UI CALLS
    # ──────────────────────────────────────────────
    def ui_set_status(self, text, color):
        def _do():
            self._status_lbl.config(text=text, fg=color)
        self.root.after(0, _do)

    def ui_set_dot(self, key, ok: bool):
        color = C["green"] if ok else C["red"]
        def _do():
            if key == "tcp":
                self._dot_tcp.config(fg=color)
            elif key == "data":
                self._dot_data.config(fg=color)
        self.root.after(0, _do)

    # ──────────────────────────────────────────────
    #  ACTIONS
    # ──────────────────────────────────────────────
    def _clear_chart(self):
        store.reset_data()
        for line in self._lines.values():
            line.set_data([], [])
        self._canvas.draw_idle()

    def _export_csv(self):
        with store.lock:
            if not store.times:
                messagebox.showinfo("Export CSV", "Chưa có dữ liệu để xuất!")
                return
            t_arr, adc_arr, duty_arr, pos_arr, rpm_arr = store.snapshot()

        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
            initialfile=f"motor_data_{time.strftime('%Y%m%d_%H%M%S')}.csv",
        )
        if not path:
            return
        try:
            with open(path, "w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(["Time(s)", "ADC", "Duty(%)", "Position(deg)", "RPM"])
                for row in zip(t_arr, adc_arr, duty_arr, pos_arr, rpm_arr):
                    writer.writerow([f"{v:.4f}" for v in row])
            messagebox.showinfo("Export CSV", f"Đã xuất {len(t_arr)} dòng dữ liệu!\n{path}")
        except Exception as e:
            messagebox.showerror("Export CSV", f"Lỗi: {e}")

    # ──────────────────────────────────────────────
    #  CLOSE
    # ──────────────────────────────────────────────
    def on_close(self):
        if self._server:
            self._server.stop()
        self.root.destroy()
        sys.exit(0)


# ════════════════════════════════════════════════════
#  ENTRY POINT
# ════════════════════════════════════════════════════
if __name__ == "__main__":
    root = tk.Tk()
    app  = App(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
