#!/usr/bin/env python3
"""Local interactive preview using the actual firmware C++ renderer."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import math
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/":
            content = (ROOT / "preview/index.html").read_bytes()
            mime = "text/html; charset=utf-8"
        elif parsed.path == "/frame":
            try:
                query = parse_qs(parsed.query)
                pitch = float(query.get("pitch", ["0"])[0])
                roll = float(query.get("roll", ["0"])[0])
                if not math.isfinite(pitch) or not math.isfinite(roll) or abs(pitch) > 90 or abs(roll) > 180:
                    raise ValueError("Angles out of range")
            except (ValueError, IndexError):
                self.send_error(400, "Invalid pitch or roll")
                return
            try:
                with tempfile.TemporaryDirectory(prefix="attitude-frame-") as folder:
                    output = Path(folder) / "frame.bmp"
                    subprocess.run([str(ROOT / "build/render_frame"), str(pitch), str(roll), str(output)],
                                   check=True, capture_output=True, timeout=10)
                    content = output.read_bytes()
                mime = "image/bmp"
            except (OSError, subprocess.SubprocessError):
                self.send_error(503, "Build the native renderer with sh tools/test.sh first")
                return
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(content)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, fmt, *args):
        if args and str(args[1]) != "200":
            super().log_message(fmt, *args)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    print(f"Attitude preview: http://127.0.0.1:{args.port}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
