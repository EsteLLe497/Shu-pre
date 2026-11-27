"""Standard-library HTTP fixtures for the C++ Ollama adapter. No Ollama or tokens required."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import subprocess
import threading
import time

hits = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # Cancellation intentionally resets a keep-alive socket.

    def do_GET(self):
        hits.append((self.path, "GET"))
        body = b'{"details":{}}'
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        model = data.get("model", "")
        hits.append((self.path, model))
        try:
            if self.path == "/api/show":
                if model == "test-redirect":
                    self.send_response(302)
                    self.send_header("Location", f"http://127.0.0.1:{self.server.server_port}/redirect-target")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if model == "test-total-deadline":
                    time.sleep(0.07)
                reply = {"details": {}}
                if model == "test-remote":
                    reply["remote_host"] = "https://example.invalid"
                body = json.dumps(reply).encode()
            else:
                assert self.path == "/api/chat"
                assert data["think"] is False and data["stream"] is False
                assert data["format"]["additionalProperties"] is False
                if model == "test-delay":
                    time.sleep(0.5)
                if model == "test-total-deadline":
                    time.sleep(0.07)
                body = json.dumps({"done": True, "message": {"content": json.dumps({"dialogue": "一緒に歩こう。"}, ensure_ascii=False)}}).encode()
                if model == "test-invalid":
                    body = b"not json"
                if model in {"test-oversize", "test-chunked"}:
                    body = b"x" * (128 * 1024 + 8192)
                if model == "test-error":
                    self.send_response(500)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            if model == "test-chunked" and self.path == "/api/chat":
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for start in range(0, len(body), 8192):
                    chunk = body[start:start + 8192]
                    self.wfile.write(f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
            else:
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if model == "test-slowbody" and self.path == "/api/chat":
                    self.wfile.write(body[:1])
                    self.wfile.flush()
                    time.sleep(0.5)
                    self.wfile.write(body[1:])
                else:
                    self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([str(root / ".codex-build/DialogueTests.exe"), "--network", str(server.server_port)], cwd=root, timeout=30)
        assert result.returncode == 0, "C++ transport checks failed"
        assert not any(path == "/redirect-target" for path, model in hits), "redirect unexpectedly followed"
        assert not any(path == "/api/chat" and model == "test-remote" for path, model in hits), "remote model was queried"
        print("HTTP fixtures: redirect isolation and remote-model rejection verified")
    finally:
        server.shutdown()
        server.server_close()
    subprocess.run([str(root / ".codex-build/DialogueTests.exe"), "--offline", str(server.server_port)], cwd=root, timeout=10, check=True)
