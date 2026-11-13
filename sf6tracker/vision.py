"""Picture helpers: ROI crops, OCR, template comparison, text parsing."""
from __future__ import annotations

import difflib
import re
import threading

import cv2
import numpy as np

from .config import ROSTER


def crop(frame, roi):
    """Cut a fractional ROI [x, y, w, h] out of a frame. roi None = the whole frame."""
    if not roi:
        return frame
    h, w = frame.shape[:2]
    x0 = max(0, min(w - 2, int(round(roi[0] * w))))
    y0 = max(0, min(h - 2, int(round(roi[1] * h))))
    x1 = max(x0 + 2, min(w, int(round((roi[0] + roi[2]) * w))))
    y1 = max(y0 + 2, min(h, int(round((roi[1] + roi[3]) * h))))
    return frame[y0:y1, x0:x1]


def similarity(a, b):
    """How alike two crops are (-1..1), independent of their size."""
    size = (128, 64)
    ga = cv2.resize(cv2.cvtColor(a, cv2.COLOR_BGR2GRAY), size, interpolation=cv2.INTER_AREA).astype(np.float32)
    gb = cv2.resize(cv2.cvtColor(b, cv2.COLOR_BGR2GRAY), size, interpolation=cv2.INTER_AREA).astype(np.float32)
    ga -= ga.mean()
    gb -= gb.mean()
    den = float(np.sqrt((ga * ga).sum() * (gb * gb).sum()))
    return float((ga * gb).sum() / den) if den > 1e-6 else 0.0


def _limit_onnx_threads(threads=1):
    """Keep the OCR on one quiet thread. By default it grabs every CPU core, which makes the game stutter."""
    try:
        import onnxruntime as ort
    except Exception:
        return
    real = ort.InferenceSession
    if getattr(real, "_sf6_limited", False):
        return

    def limited(path_or_bytes, sess_options=None, *args, **kw):
        so = sess_options or ort.SessionOptions()
        so.intra_op_num_threads = threads
        so.inter_op_num_threads = 1
        try:
            so.add_session_config_entry("session.intra_op.allow_spinning", "0")
        except Exception:
            pass
        return real(path_or_bytes, so, *args, **kw)

    limited._sf6_limited = True
    ort.InferenceSession = limited


def lower_priority():
    """Windows: run below normal priority so the game and OBS always get the CPU first."""
    try:
        import ctypes
        k = ctypes.windll.kernel32
        # without these the 64-bit process handle is cut down to 32 bits and the call silently fails
        k.GetCurrentProcess.restype = ctypes.c_void_p
        k.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        k.SetPriorityClass(k.GetCurrentProcess(), 0x00004000)  # BELOW_NORMAL_PRIORITY_CLASS
    except Exception:
        pass


class Ocr:
    """Lazy wrapper around RapidOCR. Returns items with positions as fractions of the full frame."""

    def __init__(self):
        self._engine = None
        self._lock = threading.Lock()
        self.error = None

    def _get(self):
        if self._engine is None and self.error is None:
            try:
                try:  # nothing leaves this PC
                    import onnxruntime
                    onnxruntime.disable_telemetry_events()
                except Exception:
                    pass
                _limit_onnx_threads()
                from rapidocr_onnxruntime import RapidOCR
                try:  # newer versions: let our own resizing decide how big the search picture is
                    self._engine = RapidOCR(det_limit_side_len=1600, det_limit_type="max")
                except Exception:
                    self._engine = RapidOCR()
            except Exception as e:  # missing package or model
                self.error = "OCR を読み込めません: %s" % e
        return self._engine

    def read(self, frame, roi=None, det_width=None, top=None, scale=None):
        """Read text in a frame (or one ROI of it).

        det_width: look for text on a copy shrunk to this width (cheap); the letters themselves
                   are still read from the full-size picture.
        top:       read only the N tallest pieces of text.
        scale:     enlarge the crop by this factor (default: small crops are blown up to 240 px high).
        """
        with self._lock:
            engine = self._get()
            if engine is None:
                return []
            img = crop(frame, roi)
            ih, iw = img.shape[:2]
            k = scale if scale else (min(4.0, 240.0 / ih) if ih < 240 else 1.0)  # small crops read better enlarged
            if k != 1.0:
                img = cv2.resize(img, None, fx=k, fy=k, interpolation=cv2.INTER_CUBIC)
            try:
                rows = self._staged(engine, img, det_width, top) if self._can_stage(engine) else self._plain(engine, img)
            except Exception as e:
                self.error = "OCR エラー: %s" % e
                return []
            h, w = img.shape[:2]
            rx, ry, rw, rh = roi if roi else (0.0, 0.0, 1.0, 1.0)
            items = []
            for (x0, y0, x1, y1), text, conf in rows:
                it = {"text": str(text), "conf": round(float(conf), 3),
                      "x": rx + x0 / w * rw, "y": ry + y0 / h * rh, "w": (x1 - x0) / w * rw, "h": (y1 - y0) / h * rh}
                it["cx"] = it["x"] + it["w"] / 2
                it["cy"] = it["y"] + it["h"] / 2
                items.append(it)
            return items

    @staticmethod
    def _can_stage(engine):
        return hasattr(engine, "text_det") and hasattr(engine, "text_rec")

    @staticmethod
    def _plain(engine, img):
        """Old RapidOCR versions: one call does everything."""
        pad = 16
        big = cv2.copyMakeBorder(img, pad, pad, pad, pad, cv2.BORDER_REPLICATE)
        try:
            out = engine(big, use_cls=False)
        except TypeError:
            out = engine(big)
        rows = []
        for row in (out[0] if isinstance(out, tuple) else out) or []:
            xs = [p[0] - pad for p in row[0]]
            ys = [p[1] - pad for p in row[0]]
            rows.append(((min(xs), min(ys), max(xs), max(ys)), row[1], row[2]))
        return rows

    @staticmethod
    def _staged(engine, img, det_width, top):
        """Find text on a small copy, keep the biggest pieces, read those from the full-size picture."""
        h, w = img.shape[:2]
        k = min(1.0, float(det_width) / w) if det_width else 1.0
        small = cv2.resize(img, None, fx=k, fy=k, interpolation=cv2.INTER_AREA) if k < 1.0 else img
        pad = 16
        small = cv2.copyMakeBorder(small, pad, pad, pad, pad, cv2.BORDER_REPLICATE)
        out = engine(small, use_det=True, use_cls=False, use_rec=False)
        boxes = []
        for b in (out[0] if isinstance(out, tuple) else out) or []:
            pts = b[0] if len(b) and isinstance(b[0][0], (list, tuple)) and len(b) != 4 else b
            xs = [(p[0] - pad) / k for p in pts]
            ys = [(p[1] - pad) / k for p in pts]
            x0, y0, x1, y1 = max(0, min(xs)), max(0, min(ys)), min(w, max(xs)), min(h, max(ys))
            if x1 - x0 >= 4 and y1 - y0 >= 4:
                boxes.append((x0, y0, x1, y1))
        boxes.sort(key=lambda b: b[1] - b[3])  # tallest first
        if top:
            boxes = boxes[:top]
        rows = []
        for x0, y0, x1, y1 in boxes:
            m = int((y1 - y0) * 0.12) + 2
            piece = img[max(0, int(y0) - m):int(y1) + m, max(0, int(x0) - m):int(x1) + m]
            res = engine(piece, use_det=False, use_cls=False, use_rec=True)
            res = res[0] if isinstance(res, tuple) else res
            if res and res[0][0] and float(res[0][1]) >= 0.5:
                rows.append(((x0, y0, x1, y1), res[0][0], res[0][1]))
        return rows


def tokens(text):
    return [t for t in re.split(r"[^A-Za-z0-9]+", text.upper()) if t]


def keyword_hits(items, keywords):
    """OCR items that contain one of the keywords as a whole word."""
    keys = {k.upper() for k in keywords}
    return [it for it in items if keys.intersection(tokens(it["text"]))]


def _norm(text):
    return re.sub(r"[^A-Z0-9]", "", text.upper())


_ALIASES = []
for _cid, _en, _ja, _extra in ROSTER:
    for _a in [_en] + list(_extra):
        _ALIASES.append((_norm(_a), _cid))
_JA = [(ja, cid) for cid, _, ja, _ in ROSTER if not ja.isascii()]


def match_character(text):
    """Character id for an OCR text, or None."""
    for ja, cid in _JA:
        if ja in text:
            return cid
    n = _norm(text)
    if not n:
        return None
    for alias, cid in _ALIASES:
        if n == alias:
            return cid
    # SF6 draws the control-type icon (C / M / D) right before the name; OCR glues it on: "CDHALSIM", "CJP"
    if len(n) >= 3 and n[0] in "CMDG":
        for alias, cid in _ALIASES:
            if n[1:] == alias:
                return cid
    if len(n) >= 5:  # short names (ED, JP, KEN...) must match exactly
        best, score = None, 0.0
        for alias, cid in _ALIASES:
            if len(alias) < 5:
                continue
            s = difflib.SequenceMatcher(None, n, alias).ratio()
            if s > score:
                best, score = cid, s
        if score >= 0.8:
            return best
    return None


def find_rating(items, label, lo, hi, prefer_left=True):
    """Pick the rating number out of OCR items.

    With a label (MR / LP) on screen the number next to it wins; without one the
    caller should pass items from a tight ROI so any number in range is the rating.
    """
    label = (label or "").upper()
    nums, labels = [], []
    for it in items:
        text = it["text"].upper().replace("O", "0") if re.fullmatch(r"[\dOo,.\s]+", it["text"]) else it["text"].upper()
        if label and label in tokens(text) or (label and re.search(r"\b%s\s*\d" % re.escape(label), text)):
            labels.append(it)
        for m in re.finditer(r"(?<![\d+\-])\d[\d,]{2,6}(?!\d)", text):
            v = int(m.group(0).replace(",", ""))
            if lo <= v <= hi:
                nums.append((v, it))
    if not nums:
        return None
    if labels:
        def dist(n):
            return min(abs(n[1]["cx"] - l["cx"]) + 2 * abs(n[1]["cy"] - l["cy"]) for l in labels)
        nums.sort(key=dist)
        return nums[0][0] if dist(nums[0]) < 0.25 else None
    if label:  # asked for a labelled number and the label is not on screen
        return None
    return nums[0][0] if len({n[0] for n in nums}) == 1 else None


_RATING_RE = re.compile(r"(?<![\d+\-.,:])(\d{1,3}(?:,\d{3})+|\d{3,6})\s*(LP|MR)(?![A-Z])|(?<![A-Z])(LP|MR)\s*[:\uff1a]?\s*(\d{1,3}(?:,\d{3})+|\d{3,6})(?!\d)")


def find_ratings(items, lo=0, hi=999999):
    """Every 'number + LP/MR' (either order, glued or spaced) in OCR items: [(value, label, item)]."""
    out = []
    for it in items:
        for m in _RATING_RE.finditer(it["text"].upper()):
            num, label = (m.group(1), m.group(2)) if m.group(1) else (m.group(4), m.group(3))
            v = int(num.replace(",", ""))
            if lo <= v <= hi:
                out.append((v, label, it))
    return out


def _part(frame, x0, y0, x1, y1):
    h, w = frame.shape[:2]
    return frame[int(y0 * h):max(int(y0 * h) + 1, int(y1 * h)), int(x0 * w):max(int(x0 * w) + 1, int(x1 * w))]


def result_bar_likely(frame):
    """Cheap look (no OCR) for the SF6 result bar: white "WIN" in one slot, grey "LOSE" on dark in the other.

    Only a first sieve. It lets a few other pictures through (those then get read properly) but
    does not miss the bar, which is what makes it safe to skip the reading on every other frame.
    """
    slots = []
    for x0, x1 in ((0.430, 0.466), (0.534, 0.572)):
        b = _part(frame, x0, 0.776, x1, 0.810)
        slots.append((float((b.min(axis=2) > 200).mean()), float((b.max(axis=2) < 130).mean())))
    (wl, dl), (wr, dr) = slots
    return (0.05 <= wl <= 0.65 and wr < 0.04 and dr > 0.45) or (0.05 <= wr <= 0.65 and wl < 0.04 and dl > 0.45)


def battle_hud_likely(frame):
    """Cheap look (no OCR) for the round timer: big purple digits top centre. Menus can look alike,
    so this only decides whether reading the HUD is worth trying."""
    hsv = cv2.cvtColor(_part(frame, 0.475, 0.045, 0.525, 0.12), cv2.COLOR_BGR2HSV)
    purple = (hsv[..., 0] >= 130) & (hsv[..., 0] <= 170) & (hsv[..., 1] > 90) & (hsv[..., 2] > 110)
    return float(purple.mean()) >= 0.25


def result_words(items):
    """{'WIN': x, 'LOSE': x}: where the two words of the SF6 result bar "WIN 1-0 LOSE" stand.

    OCR returns the bar in one piece ("WIN1-0 LOSE") or in several, so the words are located
    inside the text they came in.
    """
    pos = {}
    for it in items:
        t = it["text"].upper()
        for m in re.finditer(r"WIN|LOSE", t):
            pos.setdefault(m.group(0), it["x"] + it["w"] * (m.start() + m.end()) / 2.0 / len(t))
    return pos


def p1_result(items):
    """'win' / 'lose' for player 1 from the result bar (left word = 1P, right word = 2P), or None."""
    pos = result_words(items)
    w, l = pos.get("WIN"), pos.get("LOSE")
    if w is not None and l is not None:
        return "win" if w < l else "lose"
    x = w if w is not None else l
    if x is None or not 0.02 <= abs(x - 0.5) <= 0.09:  # one word alone must sit where the bar puts it
        return None
    return "win" if (x < 0.5) == (w is not None) else "lose"


def player_seat(text):
    """'p1' / 'p2' for the "PLAYER1" / "PLAYER2" label of the result screen."""
    m = re.search(r"PLAYER\s*([12])", text.upper())
    return "p" + m.group(1) if m else None


def _fold(text):
    return "".join(ch for ch in text.casefold() if ch.isalnum())


def name_matches(text, names):
    """True when an OCR text is (or contains) one of the registered player names, allowing small misreads."""
    t = _fold(text)
    if not t:
        return False
    for name in names:
        n = _fold(name)
        if not n:
            continue
        if t == n or (len(n) >= 3 and n in t):
            return True
        if len(n) >= 4 and abs(len(t) - len(n)) <= 2 and difflib.SequenceMatcher(None, t, n).ratio() >= 0.75:
            return True
    return False
