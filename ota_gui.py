import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext
import threading
import http.server
import socketserver
import os
import socket
import time
import sys

# Global variables for the server
httpd = None
server_thread = None
server_running = False

class OTARequestHandler(http.server.BaseHTTPRequestHandler):
    """Handler xu ly HTTP request tu bootloader"""
    
    def log_message(self, format, *args):
        # Override to prevent printing to stdout, we will log to GUI
        pass

    def do_GET(self):
        """Xu ly GET request"""
        gui.log(f"\n{'='*50}")
        gui.log(f"[{time.strftime('%H:%M:%S')}] Nhan request: {self.command} {self.path}")
        gui.log(f"  Tu: {self.client_address[0]}:{self.client_address[1]}")

        # Chi phuc vu /update.bin
        if self.path == "/update.bin":
            self.serve_firmware()
        else:
            gui.log(f"  [WARN] Path khong hop le: {self.path}")
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"404 Not Found")

    def serve_firmware(self):
        """Doc va gui file firmware"""
        firmware_path = gui.get_firmware_path()

        if not os.path.exists(firmware_path):
            gui.log(f"  [ERROR] Khong tim thay file: {firmware_path}")
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Firmware file not found")
            return

        file_size = os.path.getsize(firmware_path)
        gui.log(f"  [OK] Dang gui firmware: {os.path.basename(firmware_path)}")
        gui.log(f"  [OK] Kich thuoc: {file_size} bytes ({file_size/1024:.1f} KB)")

        # Gui response
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(file_size))
        self.send_header("Connection", "close")
        self.end_headers()

        # Gui noi dung file
        try:
            with open(firmware_path, "rb") as f:
                sent = 0
                while True:
                    chunk = f.read(64)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    time.sleep(0.01)  # Nghi 10ms de STM32 ghi kip Flash
                    sent += len(chunk)
            
            gui.log(f"  [OK] Da gui xong: {sent} bytes")
            gui.log(f"  [OK] XONG! Vui long doi vi dieu khien khoi dong lai.")
        except Exception as e:
            gui.log(f"  [ERROR] Loi trong qua trinh gui: {str(e)}")
        finally:
            gui.log(f"{'='*50}")

def get_local_ip():
    """Lay IP address cua may tinh tren mang LAN"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def start_server_thread(port):
    global httpd, server_running
    socketserver.TCPServer.allow_reuse_address = True
    try:
        httpd = socketserver.TCPServer(("0.0.0.0", port), OTARequestHandler)
        server_running = True
        gui.log(f"[INFO] Server da khoi dong tai cong {port}")
        httpd.serve_forever()
    except Exception as e:
        gui.log(f"[ERROR] Khong the khoi dong server: {str(e)}")
        gui.on_server_stopped()

class OTAGui:
    def __init__(self, root):
        self.root = root
        self.root.title("STM32 OTA Bootloader Server")
        self.root.geometry("650x550")
        self.root.resizable(False, False)
        
        # Style
        style = ttk.Style()
        style.theme_use('clam')
        
        # IP Info
        self.local_ip = get_local_ip()
        
        # Tim file Code_Core.bin tu dong
        self.default_bin_path = os.path.abspath(os.path.join(
            os.path.dirname(__file__), 
            "App_Firmware", "MDK-ARM", "Code_Core", "Code_Core.bin"
        ))
        
        self.setup_ui()
        self.log(f"IP LAN hien tai cua may tinh: {self.local_ip}")
        self.log("Luu y: Dia chi IP nay phai trung khop voi IP duoc ghi trong code cua vi dieu khien (freertos.c)!")
        
        if os.path.exists(self.default_bin_path):
            self.log(f"\n[OK] Da tu dong tim thay file firmware sau khi build tu Keil:")
            self.log(f"-> {self.default_bin_path}")
        else:
            self.log("\n[WARN] Khong tim thay file Code_Core.bin mac dinh. Vui long build tu Keil hoac chon file thu cong.")

    def setup_ui(self):
        # Frame config
        config_frame = ttk.LabelFrame(self.root, text="Cấu hình Server", padding=(10, 10))
        config_frame.pack(fill="x", padx=10, pady=10)
        
        # IP Label
        ttk.Label(config_frame, text="IP Address:").grid(row=0, column=0, sticky="w", pady=5)
        ttk.Label(config_frame, text=self.local_ip, foreground="blue", font=("Helvetica", 10, "bold")).grid(row=0, column=1, sticky="w", pady=5)
        
        # Port
        ttk.Label(config_frame, text="Port:").grid(row=1, column=0, sticky="w", pady=5)
        self.port_var = tk.StringVar(value="8000") # Da thay doi mac dinh thanh 8000 theo code cua ban
        ttk.Entry(config_frame, textvariable=self.port_var, width=10).grid(row=1, column=1, sticky="w", pady=5)
        
        # File Path
        ttk.Label(config_frame, text="File Firmware (.bin):").grid(row=2, column=0, sticky="w", pady=5)
        self.file_var = tk.StringVar(value=self.default_bin_path if os.path.exists(self.default_bin_path) else "")
        ttk.Entry(config_frame, textvariable=self.file_var, width=50).grid(row=2, column=1, sticky="w", pady=5, padx=5)
        ttk.Button(config_frame, text="Browse...", command=self.browse_file).grid(row=2, column=2, padx=5)
        
        # Buttons Frame
        btn_frame = ttk.Frame(self.root)
        btn_frame.pack(fill="x", padx=10, pady=5)
        
        self.btn_start = ttk.Button(btn_frame, text="START SERVER", command=self.start_server, style="Accent.TButton")
        self.btn_start.pack(side="left", padx=5, expand=True, fill="x")
        
        self.btn_stop = ttk.Button(btn_frame, text="STOP SERVER", command=self.stop_server, state="disabled")
        self.btn_stop.pack(side="left", padx=5, expand=True, fill="x")
        
        # Log Frame
        log_frame = ttk.LabelFrame(self.root, text="Console Log", padding=(5, 5))
        log_frame.pack(fill="both", expand=True, padx=10, pady=10)
        
        self.txt_log = scrolledtext.ScrolledText(log_frame, wrap=tk.WORD, width=60, height=15, font=("Consolas", 9), bg="#1e1e1e", fg="#d4d4d4")
        self.txt_log.pack(fill="both", expand=True)

    def browse_file(self):
        filepath = filedialog.askopenfilename(
            title="Chon file Firmware",
            filetypes=(("Bin files", "*.bin"), ("All files", "*.*"))
        )
        if filepath:
            self.file_var.set(filepath)
            # Hien thi thong tin file da chon vao Console Log
            file_size = os.path.getsize(filepath)
            file_mtime = os.path.getmtime(filepath)
            file_time_str = time.strftime('%d/%m/%Y %H:%M:%S', time.localtime(file_mtime))
            file_name = os.path.basename(filepath)
            self.log(f"\n{'='*50}")
            self.log(f"[OK] Da chon firmware: {file_name}")
            self.log(f"  Duong dan : {filepath}")
            self.log(f"  Kich thuoc: {file_size} bytes ({file_size/1024:.1f} KB)")
            self.log(f"  Build luc : {file_time_str}")
            self.log(f"{'='*50}")
            
    def get_firmware_path(self):
        return self.file_var.get()
        
    def log(self, message):
        self.txt_log.config(state="normal")
        self.txt_log.insert(tk.END, message + "\n")
        self.txt_log.see(tk.END)
        self.txt_log.config(state="disabled")
        
    def start_server(self):
        global server_thread
        
        port_str = self.port_var.get()
        try:
            port = int(port_str)
        except ValueError:
            self.log("[ERROR] Port phai la mot so nguyen!")
            return
            
        if not os.path.exists(self.file_var.get()):
            self.log("[ERROR] Khong tim thay file firmware. Hay chon file hop le truoc khi Start!")
            return
            
        self.btn_start.config(state="disabled")
        self.btn_stop.config(state="normal")
        self.log("\n[INFO] Dang khoi dong server...")
        
        server_thread = threading.Thread(target=start_server_thread, args=(port,), daemon=True)
        server_thread.start()
        
    def stop_server(self):
        global httpd, server_running
        if httpd:
            self.log("[INFO] Dang tat server...")
            httpd.shutdown()
            httpd.server_close()
            httpd = None
            server_running = False
            self.on_server_stopped()
            
    def on_server_stopped(self):
        self.btn_start.config(state="normal")
        self.btn_stop.config(state="disabled")
        self.log("[INFO] Server da duoc tat an toan.\n")

if __name__ == "__main__":
    root = tk.Tk()
    
    # Tao mau xanh cho nut Start (Accent style)
    style = ttk.Style()
    style.configure("Accent.TButton", font=("Helvetica", 10, "bold"), foreground="blue")
    
    gui = OTAGui(root)
    
    # Handle window close
    def on_closing():
        gui.stop_server()
        root.destroy()
        sys.exit(0)
        
    root.protocol("WM_DELETE_WINDOW", on_closing)
    root.mainloop()
