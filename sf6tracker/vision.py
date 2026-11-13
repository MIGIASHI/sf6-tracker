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
                from rapidocr_onnxruntime import RapidOCR
                self._engine = RapidOCR()
            except Exception as e:  # missing package or model
                self.error = "OCR を読み込めません: %s" % e
        return self._engine

    def read(self, frame, roi=None):
        with self._lock:
            engine = self._get()
            if engine is None:
                return []
            img = crop(frame, roi)
            ih, iw = img.shape[:2]
            scale = 1.0
            if ih < 240:  # small crops read better enlarged
                scale = min(4.0, 240.0 / ih)
                img = cv2.resize(img, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
            pad = 16
            img = cv2.copyMakeBorder(img, pad, pad, pad, pad, cv2.BORDER_REPLICATE)
            try:
                out = engine(img)
            except Exception as e:
                self.error = "OCR エラー: %s" % e
                return []
            res = out[0] if isinstance(out, tuple) else getattr(out, "boxes", None) and list(zip(out.boxes, out.txts, out.scores))
            items = []
            rx, ry, rw, rh = roi if roi else (0.0, 0.0, 1.0, 1.0)
            for row in res or []:
                box, text, conf = row[0], row[1], float(row[2])
                xs = [(p[0] - pad) / scale / iw for p in box]
                ys = [(p[1] - pad) / scale / ih for p in box]
                items.append({
                    "text": str(text), "conf": round(conf, 3),
                    "x": rx + min(xs) * rw, "y": ry + min(ys) * rh,
                    "w": (max(xs) - min(xs)) * rw, "h": (max(ys) - min(ys)) * rh,
                })
            for it in items:
                it["cx"] = it["x"] + it["w"] / 2
                it["cy"] = it["y"] + it["h"] / 2
            return items


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
    return nums[0][0] if len({n[0] for n in nums}) == 1 else None
