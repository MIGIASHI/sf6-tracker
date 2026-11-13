"""Local web server: the overlay for OBS, the control panel and a small JSON API. Listens on 127.0.0.1 only."""
from __future__ import annotations

import json
import os
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import cv2
import numpy as np

WEB = os.path.join(os.path.dirname(__file__), "web")
PAGES = {"/": "control.html", "/control": "control.html", "/overlay": "overlay.html"}


def make_handler(app):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *a):
            pass

        def send(self, body, ctype="application/json", code=200):
            if not isinstance(body, (bytes, bytearray)):
                body = json.dumps(body, ensure_ascii=False).encode("utf-8")
                ctype = "application/json; charset=utf-8"
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            return self.rfile.read(n) if n else b""

        def local(self):
            # Only answer when addressed as this PC (stops other sites from reaching the tracker by a DNS trick).
            host = (self.headers.get("Host") or "").rsplit(":", 1)[0]
            if host in ("127.0.0.1", "localhost"):
                return True
            self.send({"error": "forbidden"}, code=403)
            return False

        def do_GET(self):
            if not self.local():
                return
            u = urlparse(self.path)
            q = {k: v[0] for k, v in parse_qs(u.query).items()}
            p = u.path
            if p in PAGES:
                with open(os.path.join(WEB, PAGES[p]), "rb") as f:
                    return self.send(f.read(), "text/html; charset=utf-8")
            if p == "/api/state":
                return self.send(app.state(q.get("scope", "session")))
            if p == "/api/status":
                return self.send(app.status())
            if p == "/api/matches":
                return self.send([app.decorate(m) for m in app.store.matches()[::-1]])
            if p == "/api/config":
                return self.send(dict(app.cfg.data, roster=app.roster()))
            if p == "/api/sources":
                return self.send(app.sources())
            if p == "/api/frames":
                with app.lock:
                    return self.send([{"seq": s, "ts": t} for s, t, _ in app.frames])
            m = re.fullmatch(r"/api/frame/(latest|\d+)\.jpg", p)
            if m:
                jpg = app.frame_jpeg(None if m.group(1) == "latest" else int(m.group(1)))
                return self.send(jpg, "image/jpeg") if jpg else self.send({"error": "no frame"}, code=404)
            m = re.fullmatch(r"/api/pending/(\w+)\.png", p)
            if m and os.path.exists(app.lib.pending_path(m.group(1))):
                with open(app.lib.pending_path(m.group(1)), "rb") as f:
                    return self.send(f.read(), "image/png")
            if p == "/api/export.csv":
                self.send_response(200)
                data = ("﻿" + app.store.csv()).encode("utf-8")
                self.send_header("Content-Type", "text/csv; charset=utf-8")
                self.send_header("Content-Disposition", 'attachment; filename="sf6_history.csv"')
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                return self.wfile.write(data)
            self.send({"error": "not found"}, code=404)

        def do_POST(self):
            if not self.local():
                return
            # Browsers may only call this from the tracker's own pages.
            origin = self.headers.get("Origin")
            if origin and urlparse(origin).hostname not in ("127.0.0.1", "localhost"):
                return self.send({"error": "forbidden"}, code=403)
            p = urlparse(self.path).path
            raw = self.body()
            if p == "/api/frames/upload":
                img = cv2.imdecode(np.frombuffer(raw, np.uint8), cv2.IMREAD_COLOR)
                if img is None:
                    return self.send({"error": "画像を読めません"}, code=400)
                w = int(app.cfg["capture"]["width"])
                if img.shape[1] > w:
                    img = cv2.resize(img, (w, int(round(img.shape[0] * w / img.shape[1]))), interpolation=cv2.INTER_AREA)
                return self.send({"seq": app.push_frame(img)})
            try:
                data = json.loads(raw.decode("utf-8")) if raw else {}
            except ValueError:
                return self.send({"error": "bad json"}, code=400)
            if p == "/api/config":
                data.pop("roster", None)
                app.cfg.update(data)
                return self.send({"ok": True})
            if p == "/api/matches":
                data.setdefault("ts", time.time())
                data["auto"] = False
                return self.send(app.decorate(app.store.add(**data)))
            m = re.fullmatch(r"/api/matches/(\d+)(/delete)?", p)
            if m:
                mid = int(m.group(1))
                if m.group(2):
                    return self.send({"ok": app.store.delete(mid)})
                r = app.store.update(mid, **data)
                return self.send(app.decorate(r)) if r else self.send({"error": "not found"}, code=404)
            if p == "/api/shutdown":  # used by the OBS script when OBS closes
                self.send({"ok": True})
                app.running = False
                threading.Thread(target=self.server.shutdown, daemon=True).start()
                return
            if p == "/api/session/new":
                return self.send({"session": app.store.new_session()})
            if p == "/api/test":
                return self.send(app.test_frame(data.get("seq")))
            if p == "/api/template":
                return self.send({"ok": app.add_template(data.get("signal"), data.get("roi"), data.get("seq"))})
            if p == "/api/template/clear":
                app.clear_templates(data.get("signal"))
                return self.send({"ok": True})
            if p == "/api/pending/label":
                return self.send({"ok": app.label_pending(data.get("id", ""), data.get("char"))})
            self.send({"error": "not found"}, code=404)

    return Handler


def serve(app, port):
    httpd = ThreadingHTTPServer(("127.0.0.1", port), make_handler(app))
    httpd.daemon_threads = True
    return httpd
