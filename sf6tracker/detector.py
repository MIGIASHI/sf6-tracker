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

    def __init__(self, frame, ocr, previous=None, fetch=None):
        self.look = frame        # the picture the cheap checks run on (may be a small one)
        self._frame = None if fetch else frame
        self._fetch = fetch      # gets the full-size picture, only when something has to be read
        self.ocr = ocr
        self._texts = {}
        g = cv2.resize(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY), (48, 27), interpolation=cv2.INTER_AREA)
        self.thumb = g.astype("int16")
        # Same picture as the last tick (menus, paused screens): reuse what was read, no OCR needed.
        if previous is not None and previous.thumb.shape == self.thumb.shape \
                and int(abs(previous.thumb - self.thumb).max()) <= 4:
            # ...except where small digits count up without changing the overall picture
            self._texts = {k: v for k, v in previous._texts.items() if k not in (("sf6", "res_p1"), ("sf6", "res_p2"))}

    @property
    def frame(self):
        if self._frame is None:
            got = self._fetch() if self._fetch else None
            self._frame = got if got is not None else self.look
        return self._frame

    @frame.setter
    def frame(self, value):
        if self._fetch is None:
            self._frame = value

    def texts(self, roi, fine=False):
        """Text inside an ROI, or on the whole frame when roi is None.

        Whole-frame reads are the expensive part, so they come in two grades: a quick one that
        only catches big text (run on every tick) and a fine one for small text such as names
        and the rating (run only on VS and result screens).
        """
        key = tuple(roi) if roi else ("fine" if fine else "quick")
        if key not in self._texts:
            if roi:
                self._texts[key] = self.ocr.read(self.frame, roi, top=12)
            elif fine:
                self._texts[key] = self.ocr.read(self.frame, None, det_width=1280, top=40)
            else:
                self._texts[key] = self.ocr.read(self.frame, None, det_width=640, top=16)
        return self._texts[key]

    def region(self, name):
        """Text inside one of the fixed SF6 screen regions (see SF6_REGIONS)."""
        key = ("sf6", name)
        if key not in self._texts:
            roi, scale = SF6_REGIONS[name]
            self._texts[key] = self.ocr.read(self.frame, roi, top=16, scale=scale)
        return self._texts[key]


# Where SF6 (16:9) draws what the tracker needs: name -> ([x, y, w, h] as fractions, enlargement for reading).
# Measured on real ranked-match footage. Small regions keep a look at the screen to a few milliseconds.
SF6_REGIONS = {
    "bar": ([0.40, 0.762, 0.20, 0.058], 2.0),      # result screen: "WIN 1-0 LOSE" (left word = 1P, right = 2P)
    "res_p1": ([0.04, 0.772, 0.34, 0.085], 1.5),   # result screen: player name; my own rating stands next to my
    "res_p2": ([0.62, 0.772, 0.34, 0.085], 1.5),   # name ("51497 LP 1502 MR"), the opponent's is not shown
    "player_p1": ([0.0, 0.352, 0.14, 0.06], 2.0),  # result screen: "PLAYER1" / "PLAYER2" above the menus. Both are
    "player_p2": ([0.86, 0.352, 0.14, 0.06], 2.0), # there while a rematch is on offer; alone it is my seat
    "vs_p1": ([0.0, 0.905, 0.15, 0.05], 2.0),      # VS screen: "19760 LP" under the 1P rank
    "vs_p2": ([0.85, 0.905, 0.15, 0.05], 2.0),     # VS screen: the same for 2P
    "vs_band": ([0.0, 0.745, 1.0, 0.215], 1.5),    # VS screen: character names, player names, ratings
    "hud_p1": ([0.0, 0.012, 0.20, 0.118], 1.5),    # battle: character name, YOU marker, rating, player name
    "hud_p2": ([0.80, 0.012, 0.20, 0.118], 1.5),
}


def _top(counter, need=1):
    """Most seen entry of a Counter if it was seen at least `need` times."""
    if counter:
        key, n = counter.most_common(1)[0]
        if n >= need:
            return key
    return None


def _my_rating(votes, need=1):
    """(value, label) out of rating votes. Master players have both on screen (MR on the VS screen,
    LP in the battle HUD): MR is the one that matters then."""
    mr = Counter({k: n for k, n in votes.items() if k[1] == "MR"})
    return _top(mr, need) or (None if mr else _top(votes, need))


def _wanted_label(cfg):
    """'MR' / 'LP' when the user fixed the rating type, None for automatic (read it off the screen, MR first)."""
    v = (cfg["player"].get("rating_label") or "auto").upper()
    return v if v in ("MR", "LP") else None


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
        self._last_ctx = None
        self.m = None            # sf6 layout: what is known about the match being played
        self.res_side = Counter()  # sf6 layout: PLAYER1 / PLAYER2 seen on the result screen
        self.res_label = None
        self.last_record = None
        self.card_next = 0.0
        self.vs_next = 0.0
        self.hurry = 0.0         # seconds until the next look when it must come sooner than usual (0 = no hurry)
        self.rush_seen = 0.0     # last time the input display said it is counting Drive Rushes
        self.recovery = None     # a confirmed battle whose result screen may be missed
        self.recovery_session = store.session_id

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
        label = _wanted_label(self.cfg)
        items = ctx.texts(roi, fine=True)
        if label is None:  # automatic: any "number + MR/LP" on my side, MR first
            found = vision.find_ratings(items, d["rating_min"], d["rating_max"])
            if not roi:
                found = [f for f in found if (f[2]["cx"] < 0.5) == ((my or self.cfg["player"]["side"]) == "p1")]
            mr = [f for f in found if f[1] == "MR"]
            found = mr or found
            if found:
                return found[0][0]
            # label and number read as separate pieces: a number next to "MR", else next to "LP"
            side_items = items if roi else sorted(items, key=lambda it: it["cx"] if (my or self.cfg["player"]["side"]) == "p1" else -it["cx"])
            for want in ("MR", "LP"):
                v = vision.find_rating(side_items, want, d["rating_min"], d["rating_max"])
                if v is not None:
                    return v
            if not roi:  # an unlabelled number elsewhere on screen (player names have digits) is not a rating
                return None
            label = ""
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
            for it in ctx.texts(None, fine=True):
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
    def process(self, frame, now=None, fetch=None):
        """frame: the current picture. fetch: when frame is a small one for the cheap checks, a call
        that returns the full-size picture (used only when text has to be read)."""
        now = time.time() if now is None else now
        if self.cfg["detect"].get("layout", "sf6") == "sf6":
            return self._process_sf6(frame, now, fetch)
        d = self.cfg["detect"]
        ctx = FrameContext(frame, self.ocr, self._last_ctx, fetch)
        self._last_ctx = ctx

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
            seen = self.find_side(ctx) if sum(self.side_votes.values()) < 3 else None
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
            # the rating settles within seconds; stop the fine reads after that
            v = self.read_rating(ctx, a["side"]) if (now - a["start"] < 20 and (result or now - a["seen"] < 8)) else None
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
                  "rating_label": _wanted_label(self.cfg)}
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

    # ---- real SF6 screens (detect.layout == "sf6") -----------------------------
    # VS screen -> battle HUD -> result bar. Each tick reads only a few small fixed regions.
    @staticmethod
    def _new_match(now):
        return {"seen": now, "vs_last": None, "vs_reads": 0, "hud_reads": 0, "hud_next": 0.0, "linked": False,
                "chars": {"p1": Counter(), "p2": Counter()}, "rating": {"p1": Counter(), "p2": Counter()},
                "changed": {"p1": Counter(), "p2": Counter()}, "side": Counter(), "you": 0, "named": 0, "hud_seen": 0.0}

    def add_rush(self, n, now=None):
        """The input display saw n Drive Rush inputs (n = 0: it is only saying that it is counting).
        Counted only while a battle is on screen; menus, replays of the result screen etc. do not count."""
        now = time.time() if now is None else now
        self.rush_seen = now
        m = self.m
        if n and m and not self.active and now - m["hud_seen"] < 40:
            m["rush"] = m.get("rush", 0) + int(n)
            return True
        return False

    def _sf6_take(self, m, items, side=None):
        """Note what a VS band (side=None: decided by position) or one HUD corner says."""
        names = [n for n in self.cfg["player"].get("names", []) if n.strip()]
        d = self.cfg["detect"]
        for it in items:
            s = side or ("p1" if it["cx"] < 0.5 else "p2")
            if (it["cy"] < 0.06) if side else (0.74 < it["cy"] < 0.83):  # the line the character name is on
                cid = vision.match_character(it["text"])
                if cid:
                    m["chars"][s][cid] += 1
            if side and vision.tokens(it["text"]) == ["YOU"]:
                m["side"][s] += 3
                m["you"] += 1
            elif names and vision.name_matches(it["text"], names):
                m["side"][s] += 1
                m["named"] += 1
            want = _wanted_label(self.cfg)
            for v, label, _ in vision.find_ratings([it], d["rating_min"], d["rating_max"]):
                if want is None or label == want:
                    m["rating"][s][(v, label)] += 1

    def _sf6_result_clues(self, ctx):
        """Result screen: my own rating stands next to my name, the opponent's is not shown."""
        names = [n for n in self.cfg["player"].get("names", []) if n.strip()]
        d = self.cfg["detect"]
        for s in ("p1", "p2"):
            row = ctx.region("res_" + s)
            if vision.find_ratings(row, d["rating_min"], d["rating_max"]):
                self.res_side[s] += 2
            if names and any(vision.name_matches(it["text"], names) for it in row):
                self.res_side[s] += 1
        seats = {vision.player_seat(it["text"]) for k in ("player_p1", "player_p2") for it in ctx.region(k)} - {None}
        self.res_label = seats.pop() if len(seats) == 1 else None

    def _sf6_vs_screen(self, ctx, now):
        """Is this the VS screen? Needs the full picture and a little reading, and menus have nothing
        cheap to tell them apart, so outside a match this is tried only every 2.5 s (the screen stays ~4 s)."""
        found = bool(vision.find_ratings(ctx.region("vs_p1") + ctx.region("vs_p2")))
        self.vs_next = now if found else now + 2.5
        return found

    def _sf6_side(self):
        """(side, how I know) for the match being played."""
        m = self.m
        if m and m["you"]:
            return m["side"].most_common(1)[0][0], "you"
        if self.res_side:
            return self.res_side.most_common(1)[0][0], "result"
        if m and m["side"]:
            return m["side"].most_common(1)[0][0], "name"
        if self.res_label:
            return self.res_label, "label"
        mine = set(self.cfg["player"].get("chars", []))
        if m and mine:
            hit = [s for s in ("p1", "p2") if _top(m["chars"][s]) in mine]
            if len(hit) == 1:
                return hit[0], "char"
        return self.cfg["player"]["side"], "default"

    def _sf6_complete(self, m):
        my = m["side"].most_common(1)[0][0] if m["you"] >= 2 else None
        return bool(my and _top(m["chars"]["p1"], 2) and _top(m["chars"]["p2"], 2) and _my_rating(m["rating"][my], 2))

    def _sf6_settle_previous(self, r, my_char, now):
        """A rating seen after a match (start of the next one, or my card on the menu) is where that match left it."""
        last = self.store.last()
        if not (r and last) or last["session"] != self.store.session_id or last.get("rating") is not None:
            return
        before = last.get("rating_before")
        if before is None or last.get("rating_label") != r[1] or (my_char and last.get("my_char") != my_char):
            return
        if now - last["ts"] < 1800 and 0 < abs(r[0] - before) <= 1500:
            self.store.update(last["id"], rating=r[0], delta=r[0] - before)

    def _sf6_track_character(self):
        m = self.m
        if not m or not self.cfg['history'].get('reset_on_character_change', True) or (m['you'] < 2 and m['named'] < 2):
            return
        side = _top(m['side'], 2)
        character = _top(m['chars'][side], 2) if side else None
        if character and self.store.observe_character(character):
            self.recovery = self.active = None
            self.last_record = None
            self.recovery_session = self.store.session_id

    def _sf6_arm_recovery(self):
        m = self.m
        if self.active or not m or not m["hud_seen"] or (m["you"] < 2 and m["named"] < 2):
            return
        my = _top(m["side"], 2)
        if not my:
            return
        mc, oc = _top(m["chars"][my], 2), _top(m["chars"]["p2" if my == "p1" else "p1"], 2)
        r = _my_rating(m["rating"][my], 2)
        if not (mc and oc and r):
            return
        if self.recovery:
            a = self.recovery
            if a["candidate"] or a["before"] != r or \
                    (a["fields"]["side"], a["fields"]["my_char"], a["fields"]["opp_char"]) == (my, mc, oc):
                return
        last = self.store.last()
        self.recovery = {"before": r, "session": self.store.session_id, "started": m["hud_seen"],
                         "last_id": last["id"] if last else None, "candidate": None, "confirmations": 0,
                         "fields": {"auto": True, "side": my, "side_by": "rating", "my_char": mc, "opp_char": oc,
                                    "rating_before": r[0], "rating_label": r[1],
                                    "note": "レート変動から補完（勝敗画面未検出）"}}
        m["hud_next"] = min(m["hud_next"], m["hud_seen"] + 5)

    def _sf6_recover_rating(self, probe, now):
        a = self.recovery
        if not a:
            return
        last = self.store.last()
        if a["session"] != self.store.session_id or now - a["started"] > 900 or \
                (last["id"] if last else None) != a["last_id"]:
            self.recovery = None
            return
        side = _top(probe["side"])
        mc = _top(probe["chars"][side]) if side else None
        r = _my_rating(probe["rating"][side]) if side else None
        if not (side and mc and r and (probe["you"] or probe["named"])):
            a.update(candidate=None, confirmations=0)
            return
        if mc != a["fields"]["my_char"] or r[1] != a["before"][1]:
            self.recovery = None
            return
        delta = r[0] - a["before"][0]
        if not 0 < abs(delta) <= 1500:
            a.update(candidate=None, confirmations=0)
            return
        a["confirmations"] = a["confirmations"] + 1 if a["candidate"] == r else 1
        a["candidate"] = r
        if a["confirmations"] < 3:
            return
        fields = a["fields"]
        self.recovery = None
        if last and last.get("my_char") == fields["my_char"] and last.get("rating_label") == fields["rating_label"] \
                and last.get("rating") == r[0]:
            return
        self.last_record = now
        return self.store.add(**fields, ts=now, rating=r[0], delta=delta, result="win" if delta > 0 else "lose")

    def _sf6_record(self, p1res, now):
        self.recovery = None
        my, by = self._sf6_side()
        opp = "p2" if my == "p1" else "p1"
        result = p1res if my == "p1" else ("lose" if p1res == "win" else "win")
        fields = {"ts": now, "result": result, "auto": True, "side": my, "side_by": by,
                  "rating_label": _wanted_label(self.cfg)}
        before, label = None, None
        m = self.m
        if m:
            for key, side in (("my", my), ("opp", opp)):
                fields[key + "_char"] = _top(m["chars"][side])
            r = _my_rating(m["rating"][my], 2) or _my_rating(m["rating"][my])
            if r:
                before, label = r
                fields.update(rating_before=before, rating_label=label)
            # only when the input display was counting during this match; otherwise unknown, not zero
            if m["hud_seen"] and now - self.rush_seen < 15:
                fields["rushes"] = m.get("rush", 0)
        prev = self.store.last()
        rec = self.store.add(**fields)
        same = prev and prev["session"] == rec["session"] and prev.get("rating_label") == rec.get("rating_label") \
            and prev.get("my_char") == rec.get("my_char")
        self.active = {"id": rec["id"], "start": now, "seen": now, "readings": [], "side": my, "before": before,
                       "label": label or _wanted_label(self.cfg), "prev": prev.get("rating") if same else None, "settled": False}
        self.m = None
        self.res_side.clear()
        self.last_side = my
        self.last_record = now
        return rec

    def _sf6_result_rating(self, ctx, now):
        """Read my rating off the result screen. It counts up from the old value, so wait until it stands still."""
        a, d = self.active, self.cfg["detect"]
        if a["settled"] or now - a["start"] > 45:
            return
        cands = {(v, l) for v, l, _ in vision.find_ratings(ctx.region("res_" + a["side"]), d["rating_min"], d["rating_max"])
                 if (a["label"] is None or l == a["label"]) and (a["before"] is None or abs(v - a["before"]) <= 1500)}
        if len(cands) > 1 and any(l == "MR" for _, l in cands):
            cands = {c for c in cands if c[1] == "MR"}
        if len(cands) != 1:
            return
        a["readings"].append(cands.pop())
        r = a["readings"]
        v, label = r[-1]
        if len(r) < 3 or r[-2] != r[-1] or r[-3] != r[-1] or v == a["before"]:  # not there yet, or still counting
            return
        if a["before"] is not None:
            delta = v - a["before"]
        else:
            delta = v - a["prev"] if a["prev"] is not None else None
        self.store.update(a["id"], rating=v, delta=delta, rating_label=label)
        a["settled"] = True

    def _process_sf6(self, frame, now, fetch=None):
        d = self.cfg["detect"]
        if self.recovery_session != self.store.session_id:
            self.recovery_session = self.store.session_id
            self.recovery = self.m = None
        ctx = FrameContext(frame, self.ocr, self._last_ctx, fetch)
        self._last_ctx = ctx
        m = self.m
        if m and now - max(m["seen"], m["hud_seen"]) > 900:
            m = self.m = None
        event, vs = None, False
        if self.recovery and (self.recovery["session"] != self.store.session_id or now - self.recovery["started"] > 900):
            self.recovery = None
        self.hurry = 0.0
        # Two colour checks on the (small) picture decide whether anything gets read at all.
        # During a battle nothing does, apart from a look at the HUD now and then.
        hud_on = vision.battle_hud_likely(ctx.look)
        bar = ctx.region("bar") if vision.result_bar_likely(ctx.look) else []
        words = vision.result_words(bar)
        p1res = vision.p1_result(bar)
        if p1res:
            if self.active:
                self.active["seen"] = now
                self._sf6_result_rating(ctx, now)
                self.hurry = 0.4 if not self.active["settled"] and now - self.active["start"] < 20 else 0.0
            elif m is None and self.last_record and now - self.last_record < 600:
                pass  # the result screen of the match that is already recorded, back after a menu
            else:
                self._sf6_result_clues(ctx)
                self.streak = (p1res, self.streak[1] + 1 if self.streak[0] == p1res else 1)
                # "WIN" and "LOSE" side by side in that small bar is unmistakable, and a quick rematch
                # takes the screen away within a second or two: record at once.
                if len(words) == 2 or self.streak[1] >= d["confirm_ticks"]:
                    event = self._sf6_record(p1res, now)
                    self._sf6_result_rating(ctx, now)
                    self.streak = (None, 0)
                self.hurry = 0.3
        else:
            self.streak = (None, 0)
            self.res_side.clear()
            self.res_label = None
            if self.active and now - self.active["seen"] > 6:
                self.active = None
            if self.active:
                pass
            elif hud_on:
                if m:
                    m["hud_seen"] = now
                    # After the last K.O. a quick rematch leaves the result bar up for about a second,
                    # so a match is watched closely. These looks cost next to nothing (no reading).
                    self.hurry = 0.5
                done = m is not None and (self._sf6_complete(m) or m["hud_reads"] >= 10)
                if not done or now >= m["hud_next"]:
                    probe = self._new_match(now)
                    for s in ("p1", "p2"):
                        self._sf6_take(probe, ctx.region("hud_" + s), s)
                    event = self._sf6_recover_rating(probe, now)
                    new = {s: _top(probe["chars"][s]) for s in ("p1", "p2")}
                    if probe["you"] or any(new.values()):  # really the battle HUD
                        if m:
                            for s in ("p1", "p2"):
                                old, cur = _my_rating(m["rating"][s], 2), _my_rating(probe["rating"][s])
                                if old and cur and cur[1] == old[1] and cur != old:
                                    m["changed"][s][cur] += 1
                            if any(new[s] and _top(m["chars"][s], 2) not in (None, new[s]) for s in new) \
                                    or any(_top(m["changed"][s], 2) for s in new):
                                m = None  # other characters or ratings than noted: this is already the next match
                        if m is None:
                            m = self.m = self._new_match(now)
                        m["seen"] = m["hud_seen"] = now
                        m["hud_reads"] += 1
                        m["hud_next"] = now + ((0.5 if self.recovery["candidate"] else 5) if self.recovery else 30)
                        for s in ("p1", "p2"):
                            self._sf6_take(m, ctx.region("hud_" + s), s)
            elif m and now - m["hud_seen"] < 40:
                # HUD gone in the middle of a match: a cinematic, or the win pose right before the result bar
                self.hurry = 0.3
            elif now >= self.vs_next and self._sf6_vs_screen(ctx, now):
                vs = True
                self.hurry = 0.5
                if m is None or (m["vs_last"] is None and m["hud_reads"]) or (m["vs_last"] and now - m["vs_last"] > 30):
                    m = self.m = self._new_match(now)  # a fresh VS screen starts a new match
                m["seen"] = m["vs_last"] = now
                if m["vs_reads"] < 4:
                    m["vs_reads"] += 1
                    self._sf6_take(m, ctx.region("vs_band"))
                probe = self._new_match(now)
                self._sf6_take(probe, ctx.region("vs_band"))
                event = self._sf6_recover_rating(probe, now)
            elif m is None and now >= self.card_next:
                last = self.store.last()
                if last and last.get("rating") is None and last.get("rating_before") is not None \
                        and last["session"] == self.store.session_id and now - last["ts"] < 600:
                    # the main menu shows my own card (name, rank, rating) in the top right corner
                    self.card_next = now + 3
                    probe = self._new_match(now)
                    self._sf6_take(probe, ctx.region("hud_p2"), "p2")
                    if probe["named"] or not [n for n in self.cfg["player"].get("names", []) if n.strip()]:
                        self._sf6_settle_previous(_my_rating(probe["rating"]["p2"]), None, now)
            self._sf6_track_character()
            if m and not m["linked"] and self._sf6_complete(m):
                m["linked"] = True
                my = m["side"].most_common(1)[0][0]
                self._sf6_settle_previous(_my_rating(m["rating"][my], 2), _top(m["chars"][my]), now)
            self._sf6_arm_recovery()

        my, my_by = self._sf6_side() if not self.active else (self.active["side"], "recorded")
        mine = None if not p1res else (p1res if my == "p1" else ("lose" if p1res == "win" else "win"))
        texts = [it["text"] for it in bar]
        m = self.m
        self.debug = {
            "time": now, "vs": vs, "result": mine,
            "signals": {"vs": {"texts": []}, "win": {"texts": texts if mine == "win" else []},
                        "lose": {"texts": texts if mine == "lose" else []}},
            "state": "result" if self.active else ("match" if m else "idle"),
            "side": my, "side_by": my_by,
            "pending": {s: dict(m["chars"][s]) for s in ("p1", "p2")} if m else None,
            "rating": {s: ["%d %s" % k for k in m["rating"][s]] for s in ("p1", "p2")} if m else None,
        }
        return event

    # ---- one frame, for the "test" button --------------------------------------
    def inspect(self, ctx):
        """What a single frame shows: {'signals', 'chars', 'side', 'rating'}."""
        if self.cfg["detect"].get("layout", "sf6") != "sf6":
            out = {"signals": {}, "chars": {}}
            for name in self.cfg["signals"]:
                ok, info, _ = self.check_signal(name, ctx)
                out["signals"][name] = dict(info, found=ok)
            for side in ("p1", "p2"):
                out["chars"][side] = self.read_char(side, ctx)[0]
            seen = self.find_side(ctx)
            side, by = (seen, "name") if seen else self.my_side(out["chars"])
            out["side"] = {"side": side, "by": by}
            out["rating"] = self.read_rating(ctx, side)
            return out
        d = self.cfg["detect"]
        keep = self.m, Counter(self.res_side), self.res_label
        try:
            m = self.m = self._new_match(0)
            self.res_side.clear()
            self.res_label = None
            bar = ctx.region("bar")
            p1res = vision.p1_result(bar)
            vs = not p1res and bool(vision.find_ratings(ctx.region("vs_p1") + ctx.region("vs_p2")))
            if p1res:
                self._sf6_result_clues(ctx)
            elif vs:
                self._sf6_take(m, ctx.region("vs_band"))
            else:
                for s in ("p1", "p2"):
                    self._sf6_take(m, ctx.region("hud_" + s), s)
            side, by = self._sf6_side()
            mine = None if not p1res else (p1res if side == "p1" else ("lose" if p1res == "win" else "win"))
            texts = [it["text"] for it in bar]
            if p1res:
                votes = Counter((v, l) for v, l, _ in vision.find_ratings(ctx.region("res_" + side), d["rating_min"], d["rating_max"]))
                r = _my_rating(votes)
            else:
                r = _my_rating(m["rating"][side])
            return {"signals": {"vs": {"found": vs, "texts": []},
                                "win": {"found": mine == "win", "texts": texts if mine == "win" else []},
                                "lose": {"found": mine == "lose", "texts": texts if mine == "lose" else []}},
                    "chars": {s: _top(m["chars"][s]) for s in ("p1", "p2")},
                    "side": {"side": side, "by": by},
                    "rating": r[0] if r else None, "rating_label": r[1] if r else None}
        finally:
            self.m, self.res_side, self.res_label = keep
