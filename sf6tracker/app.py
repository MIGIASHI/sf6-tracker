"""The running tracker: a capture loop plus the things the web panel asks for."""
from __future__ import annotations

import threading
import time
from collections import deque

import cv2
import numpy as np

from . import vision
from .capture import FileCapture, ObsCapture
from .config import Config, ROSTER, char_name
from .detector import Detector, Library
from .store import Store, summarize


class App:
    def __init__(self, data_dir, source_file=None):
        self.cfg = Config(data_dir)
        self.store = Store(data_dir)
        self.lib = Library(data_dir)
        self.ocr = vision.Ocr()
        self.detector = Detector(self.cfg, self.store, self.lib, self.ocr)
        cap = self.cfg["capture"]
        self.capture = FileCapture(source_file, cap["interval"], cap["width"]) if source_file else ObsCapture(self.cfg)
        self.is_file = bool(source_file)
        self.frames = deque(maxlen=120)  # (seq, time, jpeg bytes) for the calibration screen
        self.seq = 0
        self.lock = threading.RLock()
        self.cap_lock = threading.Lock()
        self.running = True
        self.tick_ms = 0
        self.error = None
        self.t_base = time.time() - 3600 if source_file else 0

    # ---- loop ----------------------------------------------------------------
    def push_frame(self, frame, now=None):
        ok, buf = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 85])
        with self.lock:
            self.seq += 1
            self.frames.append((self.seq, now or time.time(), buf.tobytes()))
            return self.seq

    def frame(self, seq=None):
        with self.lock:
            if not self.frames:
                return None
            item = self.frames[-1]
            if seq is not None:
                item = next((f for f in self.frames if f[0] == seq), None)
        if item is None:
            return None
        return cv2.imdecode(np.frombuffer(item[2], np.uint8), cv2.IMREAD_COLOR)

    def frame_jpeg(self, seq=None):
        with self.lock:
            if not self.frames:
                return None
            if seq is None:
                return self.frames[-1][2]
            return next((f[2] for f in self.frames if f[0] == seq), None)

    def loop(self):
        while self.running:
            t0 = time.time()
            try:
                with self.cap_lock:
                    frame = self.capture.grab()
                if frame is not None:
                    # a played-back file runs on its own clock so timing rules behave as in a live match
                    now = self.t_base + self.capture.t if self.is_file else t0
                    self.push_frame(frame, now)
                    self.detector.process(frame, now)
                    self.error = None
            except Exception as e:  # keep the stream tool alive whatever happens
                self.error = "%s: %s" % (type(e).__name__, e)
            self.tick_ms = int((time.time() - t0) * 1000)
            if self.is_file and getattr(self.capture, "done", False):
                time.sleep(1.0)
                continue
            # Never work more than a third of the time, however slow one look at the screen gets.
            busy = time.time() - t0
            wait = 0.05 if self.is_file else max(2.0 * busy, float(self.cfg["capture"]["interval"]) - busy, 0.1)
            time.sleep(wait)

    def start(self):
        vision.lower_priority()
        threading.Thread(target=self.loop, daemon=True).start()

    # ---- data for the web pages ----------------------------------------------
    def decorate(self, m):
        lang = self.cfg["display"]["char_lang"]
        m = dict(m)
        m["my_name"] = char_name(m.get("my_char"), lang)
        m["opp_name"] = char_name(m.get("opp_char"), lang)
        return m

    def state(self, scope="session"):
        allm = self.store.matches()
        if scope == "today":
            day = time.strftime("%Y-%m-%d")
            ms = [m for m in allm if time.strftime("%Y-%m-%d", time.localtime(m["ts"])) == day]
        elif scope == "all":
            ms = allm
        else:
            ms = [m for m in allm if m["session"] == self.store.session_id]
        s = summarize(ms)
        if s["rating"] is None:  # nothing rated in this scope yet: still show the last known rating
            rated = [m for m in allm if m.get("rating") is not None]
            if rated:
                s["rating"] = rated[-1]["rating"]
        s["label"] = self.cfg["player"].get("rating_label", "MR")
        s["scope"] = scope
        s["matches"] = [self.decorate(m) for m in ms[::-1][:50]]
        return s

    def status(self):
        return {"capture": self.capture.status, "error": self.error or self.ocr.error, "tick_ms": self.tick_ms,
                "debug": self.detector.debug, "frames": len(self.frames), "session": self.store.session_id,
                "pending": self.lib.pending(), "file_mode": self.is_file}

    def sources(self):
        with self.cap_lock:
            return self.capture.sources()

    def test_frame(self, seq=None):
        """Run every check on one buffered frame and report what was seen."""
        frame = self.frame(seq)
        if frame is None:
            return {"error": "フレームがありません"}
        from .detector import FrameContext
        ctx = FrameContext(frame, self.ocr)
        d = self.detector
        out = {"signals": {}, "chars": {}, "texts": {}}
        for name in self.cfg["signals"]:
            ok, info, _ = d.check_signal(name, ctx)
            out["signals"][name] = dict(info, found=ok)
        for side in ("p1", "p2"):
            cid, _ = d.read_char(side, ctx)
            out["chars"][side] = {"id": cid, "name": char_name(cid, self.cfg["display"]["char_lang"]) if cid else None}
        seen = d.find_side(ctx)
        side, by = (seen, "name") if seen else d.my_side({s: out["chars"][s]["id"] for s in ("p1", "p2")})
        out["side"] = {"side": side, "by": by}
        out["rating"] = d.read_rating(ctx, side)
        for name, roi in self.cfg["rois"].items():
            if roi:
                out["texts"][name] = [it["text"] for it in ctx.texts(roi)]
        out["all_texts"] = [{k: (round(v, 4) if isinstance(v, float) else v) for k, v in it.items()} for it in ctx.texts(None, fine=True)]
        return out

    def add_template(self, signal, roi, seq=None):
        frame = self.frame(seq)
        if frame is None or signal not in self.cfg["signals"] or not roi:
            return False
        name = self.lib.save("sig_" + signal, vision.crop(frame, roi).copy())
        sig = self.cfg["signals"][signal]
        sig["templates"] = list(sig.get("templates", [])) + [{"file": name, "roi": roi}]
        self.cfg.save()
        return True

    def clear_templates(self, signal):
        sig = self.cfg["signals"].get(signal)
        if sig:
            for t in sig.get("templates", []):
                self.lib.remove(t["file"])
            sig["templates"] = []
            self.cfg.save()

    def label_pending(self, pid, char_id):
        if not self.lib.label_pending(pid, char_id or None):
            return False
        self.store.resolve_pending(pid, char_id or None)
        return True

    def roster(self):
        lang = self.cfg["display"]["char_lang"]
        return [{"id": cid, "name": ja if lang == "ja" else en} for cid, en, ja, _ in ROSTER]
