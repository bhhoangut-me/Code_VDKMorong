#!/usr/bin/env python3
"""
OTA HTTP Server cho STM32F103 Bootloader
=========================================
Server HTTP don gian phuc vu file firmware (.bin) cho bootloader OTA.

Su dung:
    python ota_server.py                     # Mac dinh: port 8080, file update.bin
    python ota_server.py --port 8080         # Chi dinh port
    python ota_server.py --file firmware.bin # Chi dinh file firmware

Bootloader se gui HTTP GET /update.bin, server tra ve noi dung file binary.
"""

import http.server
import socketserver
import os
import sys
import argparse
import socket
import time


class OTARequestHandler(http.server.BaseHTTPRequestHandler):
    """Handler xu ly HTTP request tu bootloader"""

    firmware_file = "update.bin"

    def do_GET(self):
        """Xu ly GET request"""
        print(f"\n{'='*60}")
        print(f"[{time.strftime('%H:%M:%S')}] Nhan request: {self.command} {self.path}")
        print(f"  Tu: {self.client_address[0]}:{self.client_address[1]}")
        print(f"  Headers:")
        for key, value in self.headers.items():
            print(f"    {key}: {value}")

        # Chi phuc vu /update.bin
        if self.path == "/update.bin":
            self.serve_firmware()
        else:
            print(f"  [WARN] Path khong hop le: {self.path}")
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"404 Not Found")

    def serve_firmware(self):
        """Doc va gui file firmware"""
        firmware_path = self.firmware_file

        if not os.path.exists(firmware_path):
            print(f"  [ERROR] Khong tim thay file: {firmware_path}")
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Firmware file not found")
            return

        file_size = os.path.getsize(firmware_path)
        print(f"  [OK] Dang gui firmware: {firmware_path}")
        print(f"  [OK] Kich thuoc: {file_size} bytes ({file_size/1024:.1f} KB)")

        # Gui response
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(file_size))
        self.send_header("Connection", "close")
        self.end_headers()

        # Gui noi dung file
        with open(firmware_path, "rb") as f:
            sent = 0
            while True:
                chunk = f.read(64)
                if not chunk:
                    break
                self.wfile.write(chunk)
                time.sleep(0.01)  # Ngi 10ms de STM32 ghi kip Flash, tranh mat byte UART
                sent += len(chunk)

        print(f"  [OK] Da gui xong: {sent} bytes")
        print(f"{'='*60}")

    def log_message(self, format, *args):
        """Tat log mac dinh cua BaseHTTPRequestHandler (da tu print)"""
        pass


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


def main():
    parser = argparse.ArgumentParser(description="OTA HTTP Server cho STM32 Bootloader")
    parser.add_argument("--port", type=int, default=8080, help="Port lang nghe (mac dinh: 8080)")
    parser.add_argument("--file", type=str, default="update.bin", help="File firmware (mac dinh: update.bin)")
    args = parser.parse_args()

    OTARequestHandler.firmware_file = args.file

    # Kiem tra file firmware
    if os.path.exists(args.file):
        file_size = os.path.getsize(args.file)
        print(f"[OK] Tim thay firmware: {args.file} ({file_size} bytes / {file_size/1024:.1f} KB)")
    else:
        print(f"[WARN] Chua tim thay file firmware: {args.file}")
        print(f"[WARN] Hay copy file .bin vao thu muc nay truoc khi OTA!")
        print(f"[WARN] Thu muc hien tai: {os.getcwd()}")

    local_ip = get_local_ip()

    print(f"\n{'='*60}")
    print(f"  OTA HTTP Server - STM32F103 Bootloader")
    print(f"{'='*60}")
    print(f"  IP LAN  : {local_ip}")
    print(f"  Port    : {args.port}")
    print(f"  File    : {args.file}")
    print(f"  URL     : http://{local_ip}:{args.port}/update.bin")
    print(f"{'='*60}")
    print(f"\n  Bootloader se GET http://{local_ip}:{args.port}/update.bin")
    print(f"  Dam bao IP trong bootloader code trung voi IP LAN cua ban!")
    print(f"\n  Nhan Ctrl+C de dung server.\n")

    # Tao server voi SO_REUSEADDR
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("0.0.0.0", args.port), OTARequestHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[INFO] Server da dung.")
            httpd.server_close()


if __name__ == "__main__":
    main()
