"""Serve the Weather Garden preview from a loopback-only C++ replay bridge."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/garden.css": ("garden.css", "text/css; charset=utf-8"),
    "/garden.js": ("garden.js", "text/javascript; charset=utf-8"),
}
LIMIT = 32 * 1024


class PreviewServer(ThreadingHTTPServer):
    def __init__(self, cli, project, port):
        self.cli = cli
        self.project = project
        self.rules = (project / "assets" / "expedition.json").read_bytes()
        super().__init__(("127.0.0.1", port), GardenHandler)


class GardenHandler(BaseHTTPRequestHandler):
    def answer(self, status, kind, body):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def json_reply(self, status, reply):
        self.answer(status, "application/json; charset=utf-8", json.dumps(reply).encode("utf-8"))

    def local_host(self):
        port = self.server.server_address[1]
        return self.headers.get("Host") == f"127.0.0.1:{port}"

    def do_GET(self):
        if not self.local_host():
            self.json_reply(400, {"code": "invalid_host"})
        elif self.path in FILES:
            name, kind = FILES[self.path]
            self.answer(200, kind, (ROOT / name).read_bytes())
        elif self.path == "/api/rules":
            self.answer(200, "application/json; charset=utf-8", self.server.rules)
        else:
            self.json_reply(404, {"code": "not_found"})

    def do_POST(self):
        port = self.server.server_address[1]
        if not self.local_host() or self.headers.get("Origin") != f"http://127.0.0.1:{port}":
            self.json_reply(400, {"code": "invalid_origin"})
            return
        if self.path != "/api/replay":
            self.json_reply(404, {"code": "not_found"})
            return
        try:
            count = int(self.headers.get("Content-Length", "0"))
            if not 0 < count <= LIMIT or self.headers.get_content_type() != "application/json":
                raise ValueError("Request body must be bounded JSON")
            actions = json.loads(self.rfile.read(count))
            if not isinstance(actions, list) or len(actions) > 256:
                raise ValueError("Expected up to 256 actions")
        except (ValueError, UnicodeError):
            self.json_reply(400, {"code": "invalid_actions"})
            return

        request = {
            "operation": "expedition.replay", "version": 1, "mode": "read",
            "expected_revision": 0, "input": {"actions": actions},
        }
        try:
            completed = subprocess.run(
                [self.server.cli, "project", "request", self.server.project],
                input=json.dumps(request).encode("utf-8"), capture_output=True, timeout=10,
                check=False,
            )
            response = json.loads(completed.stdout)
        except (OSError, subprocess.TimeoutExpired, ValueError):
            self.json_reply(503, {"code": "runtime_unavailable"})
            return
        if completed.returncode:
            self.json_reply(400, {"code": response["code"]})
            return
        latest = response["steps"][-1] if response["steps"] else None
        self.json_reply(200, {"code": "ok", "state": response["state"], "latest": latest})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--open", action="store_true", help="open the local preview in a browser")
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)
    with PreviewServer(cli, ROOT.parent / "w25-lab", args.port) as server:
        url = f"http://127.0.0.1:{server.server_address[1]}/"
        print(url, flush=True)
        if args.open:
            import webbrowser

            webbrowser.open(url, new=2)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
