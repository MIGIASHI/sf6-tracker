"""Turns a stream of frames into match records.

Flow of a match on screen:  VS screen -> rounds -> result screen (WIN / LOSE, rating change).
"""
from __future__ import annotations

import os
import time
from collections import Counter

import cv2

from . import vision


class Library:
    """Reference pictures saved by the user: signal templates and character pictures."""

    def __init__(self, data_dir):
        self.dir = os.path.join(data_dir, "templates")
        self.pending_dir = os.path.join(data_dir, "pending")
        os.makedirs(self.dir, exist_ok=True)
        os.makedirs(self.pending_dir, exist_ok=True)
        self._cache = {}

    def image(self, name):
        if name not in self._cache:
            self._cache[name] = cv2.imread(os.path.join(self.dir, name))
        return self._cache[name]

    def save(self, prefix, img):
        n = 1
        while os.path.exists(os.path.join(self.dir, "%s__%d.png" % (prefix, n))):
            n += 1
        name = "%s__%d.png" % (prefix, n)
        cv2.imwrite(os.path.join(self.dir, name), img)
        self._cache[name] = img
        return name

    def remove(self, name):
        self._cache.pop(name, None)
        try:
            os.remove(os.path.join(self.dir, os.path.basename(name)))
        except OSError:
            pass

    def chars(self):
        """[(char_id, file name)] of saved character pictures."""
        out = []
        for f in sorted(os.listdir(self.dir)):
            if f.startswith("char_") and f.endswith(".png"):
                out.append((f[5:].split("__")[0], f))
        return out

    def add_pending(self, img):
        pid = "p%d" % int(time.time() * 1000)
        while os.path.exists(os.path.join(self.pending_dir, pid + ".png")):
            pid += "x"
        cv2.imwrite(os.path.join(self.pending_dir, pid + ".png"), img)
        return pid

    def pending(self):
        return sorted(f[:-4] for f in os.listdir(self.pending_dir) if f.endswith(".png"))

    def pending_path(self, pid):
        return os.path.join(self.pending_dir, os.path.basename(pid) + ".png")

    def label_pending(self, pid, char_id):
        path = self.pending_path(pid)
        img = cv2.imread(path)
        if img is None:
            return False
        if char_id:
            self.save("char_" + char_id, img)
        os.remove(path)
        return True


class FrameContext:
    """One frame plus cached OCR results, so each ROI is read at most once."""

    def __init__(self, frame, ocr):
        self.frame = frame
        self.ocr = ocr
        self._texts = {}

    def texts(self, roi):
        key = tuple(roi) if roi else None
        if key not in self._texts:
            self._texts[key] = self.ocr.read(self.frame, roi)
        return self._texts[key]


class Detector:
    def __init__(self, config, store, library, ocr=None):
        self.cfg = config
        self.store = store
        self.lib = library
        self.ocr = ocr or vision.Ocr()
        self.pending = None      # characters gathered on the VS screen
        self.streak = (None, 0)  # (result, ticks in a row)
        self.active = None       # match that was just recorded and may still get its rating
        self.side_votes = Counter()  # where my player name was seen during this match
        self.side_seen = 0.0
        self.last_side = None
        self.debug = {}

    # ---- single-frame checks -------------------------------------------------
    def roi(self, name):
        return self.cfg["rois"].get(name) if name else None

    def check_signal(self, name, ctx):
        """Returns (found, detail, hit items)."""
        sig = self.cfg["signals"][name]
        if sig.get("templates"):
            best = 0.0
            for t in sig["templates"]:
                img = self.lib.image(t["file"])
                if img is not None:
                    best = max(best, vision.similarity(vision.crop(ctx.frame, t["roi"]), img))
            return best >= self.cfg["detect"]["template_threshold"], {"score": round(best, 3)}, []
        items = ctx.texts(self.roi(sig.get("roi")))
        hits = vision.keyword_hits(items, sig.get("keywords", []))
        return bool(hits), {"texts": [h["text"] for h in hits]}, hits

    def read_char(self, side, ctx):
        """Returns (char_id or None, crop or None) for 'p1' / 'p2'."""
        roi = self.roi(side + "_char")
        if roi:
            img = vision.crop(ctx.frame, roi)
            best, score = None, 0.0
            for cid, name in self.lib.chars():
                ref = self.lib.image(name)
                if ref is not None:
                    s = vision.similarity(img, ref)
                    if s > score:
                        best, score = cid, s
            if best and score >= self.cfg["detect"]["char_threshold"]:
                return best, img
            for it in ctx.texts(roi):
                cid = vision.match_character(it["text"])
                if cid:
                    return cid, img
            return None, img.copy()
        # no ROI: search the whole frame, left half = P1, right half = P2
        cands = []
        for it in ctx.texts(None):
            if (it["cx"] < 0.5) == (side == "p1"):
                cid = vision.match_character(it["text"])
                if cid:
                    cands.append((it["h"], cid))
        return (max(cands)[1], None) if cands else (None, None)

    def read_rating(self, ctx, my=None):
        d = self.cfg["detect"]
        roi = self.roi("rating")
        label = self.cfg["player"].get("rating_label", "MR")
        items = ctx.texts(roi)
        if roi:  # a tight ROI needs no label next to the number
            v = vision.find_rating(items, label, d["rating_min"], d["rating_max"])
            return v if v is not None else vision.find_rating(items, "", d["rating_min"], d["rating_max"])
        if (my or self.cfg["player"]["side"]) == "p2":
            items = sorted(items, key=lambda it: -it["cx"])
        else:
            items = sorted(items, key=lambda it: it["cx"])
        return vision.find_rating(items, label, d["rating_min"], d["rating_max"])

    def find_side(self, ctx):
        """'p1' / 'p2' if my registered player name is on this frame, else None."""
        names = [n for n in self.cfg["player"].get("names", []) if n.strip()]
        if not names:
            return None
        found = set()
        rois = {s: self.roi(s + "_name") for s in ("p1", "p2")}
        if rois["p1"] or rois["p2"]:
            for s, roi in rois.items():
                if roi and any(vision.name_matches(it["text"], names) for it in ctx.texts(roi)):
                    found.add(s)
        else:
            for it in ctx.texts(None):
                if vision.name_matches(it["text"], names):
                    found.add("p1" if it["cx"] < 0.5 else "p2")
        return found.pop() if len(found) == 1 else None

    def my_side(self, chars=None):
        """Which side am I on in this match, and how do I know: (side, 'name' | 'char' | 'default')."""
        if self.side_votes:
            return self.side_votes.most_common(1)[0][0], "name"
        mine = set(self.cfg["player"].get("chars", []))
        if mine and chars:
            hit = [s for s in ("p1", "p2") if chars.get(s) in mine]
            if len(hit) == 1:
                return hit[0], "char"
        return self.cfg["player"]["side"], "default"

    def _voted_chars(self):
        p = self.pending
        return {s: (p["votes"][s].most_common(1)[0][0] if p["votes"][s] else None) for s in ("p1", "p2")} if p else None

    # ---- state machine -------------------------------------------------------
    def process(self, frame, now=None):
        now = time.time() if now is None else now
        d = self.cfg["detect"]
        ctx = FrameContext(frame, self.ocr)

        vs, vs_info, _ = self.check_signal("vs", ctx)
        win, win_info, win_hits = self.check_signal("win", ctx)
        lose, lose_info, lose_hits = self.check_signal("lose", ctx)
        event = None
        chars = {}
        if win or lose:
            vs = False  # a result screen is never a VS screen
        if not (win or lose) and not (self.active and not vs):
            chars = {side: self.read_char(side, ctx) for side in ("p1", "p2")}
        # The VS word can be missed; both character names on screen (VS screen or battle HUD) also count.
        named = bool(chars) and all(chars[s][0] for s in chars)
        if (vs or named or win or lose) and not self.active:
            if now - self.side_seen > (60 if vs else 900):  # a fresh VS screen starts a new match
                self.side_votes.clear()
            seen = self.find_side(ctx)
            if seen:
                self.side_votes[seen] += 1
                self.side_seen = now
        my, my_by = self.my_side(self._voted_chars())
        if my_by == "default" and not self.pending and self.last_side and self.active is None:
            last = self.store.last()
            if last and now - last["ts"] < d["carry_minutes"] * 60:
                my = self.last_side  # rematch without a VS screen: same seats as before
        result = None
        if win and lose:
            if win_hits and lose_hits:  # both words on screen: the one on my side counts
                wx = min(h["cx"] for h in win_hits) if my == "p1" else max(h["cx"] for h in win_hits)
                lx = min(h["cx"] for h in lose_hits) if my == "p1" else max(h["cx"] for h in lose_hits)
                result = "win" if (wx < lx) == (my == "p1") else "lose"
        elif win or lose:
            result = "win" if win else "lose"
        if vs and self.active:
            self.active = None
        if vs or (named and not self.active):
            if self.pending is None or now - self.pending["seen"] > 30:
                self.pending = {"votes": {"p1": Counter(), "p2": Counter()}, "crops": {}, "seen": now}
            self.pending["seen"] = now
            for side, (cid, img) in chars.items():
                if cid:
                    self.pending["votes"][side][cid] += 1
                elif img is not None:
                    self.pending["crops"][side] = img

        if self.active:
            a = self.active
            if result:
                a["seen"] = now
            v = self.read_rating(ctx, a["side"]) if (result or now - a["seen"] < 8) else None
            if v is not None:
                a["readings"].append(v)
                self._apply_rating(a)
            if now - a["start"] > d["cooldown"] and now - a["seen"] > 3:
                self.active = None
            self.streak = (None, 0)
        elif result:
            self.streak = (result, self.streak[1] + 1 if self.streak[0] == result else 1)
            if self.streak[1] >= d["confirm_ticks"]:
                event = self._record(result, my, my_by, now)
                v = self.read_rating(ctx, my)
                if v is not None:
                    self.active["readings"].append(v)
                self.streak = (None, 0)
        else:
            self.streak = (None, 0)

        self.debug = {
            "time": now, "vs": vs, "result": result, "signals": {"vs": vs_info, "win": win_info, "lose": lose_info},
            "state": "result" if self.active else ("match" if self.pending else "idle"),
            "side": my, "side_by": my_by,
            "pending": {s: dict(c) for s, c in self.pending["votes"].items()} if self.pending else None,
        }
        return event

    def _record(self, result, my, my_by, now):
        opp = "p2" if my == "p1" else "p1"
        fields = {"ts": now, "result": result, "auto": True, "side": my, "side_by": my_by,
                  "rating_label": self.cfg["player"].get("rating_label", "MR")}
        p = self.pending
        if p and now - p["seen"] > 900:  # a VS screen from long ago belongs to another match
            p = None
        if p:
            for key, side in (("my", my), ("opp", opp)):
                if p["votes"][side]:
                    fields[key + "_char"] = p["votes"][side].most_common(1)[0][0]
                elif side in p["crops"]:
                    fields[key + "_pending"] = self.lib.add_pending(p["crops"][side])
        else:
            last = self.store.last()
            if last and now - last["ts"] < self.cfg["detect"]["carry_minutes"] * 60 and last["session"] == self.store.session_id:
                fields.update(my_char=last.get("my_char"), opp_char=last.get("opp_char"),
                              my_pending=last.get("my_pending"), opp_pending=last.get("opp_pending"), carried=True)
        prev = self.store.last()
        m = self.store.add(**fields)
        self.pending = None
        self.side_votes.clear()
        self.last_side = my
        self.active = {"id": m["id"], "start": now, "seen": now, "readings": [], "side": my,
                       "prev": prev["rating"] if prev and prev["session"] == m["session"]
                       and prev.get("rating_label") == m.get("rating_label") else None}
        return m

    def _apply_rating(self, a):
        """Rating = values read at least twice. First stable value = before, last = after."""
        counts = Counter(a["readings"])
        stable = [v for v in dict.fromkeys(a["readings"]) if counts[v] >= 2]
        if not stable:
            return
        after, before = stable[-1], stable[0]
        if before != after:
            delta = after - before
        elif a["prev"] is not None:
            delta = after - a["prev"]
        else:
            delta = None
        m = self.store.get(a["id"])
        if m and (m.get("rating") != after or m.get("delta") != delta):
            self.store.update(a["id"], rating=after, delta=delta)
