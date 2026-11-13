"""End-to-end check of the recognition on drawn frames (no game footage needed)."""
import os
import sys
import tempfile

import cv2
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sf6tracker.config import Config
from sf6tracker.detector import Detector, Library
from sf6tracker.store import Store, summarize

W, H = 1280, 720
rng = np.random.default_rng(0)


def base():
    img = np.full((H, W, 3), 30, np.uint8)
    cv2.rectangle(img, (0, 0), (W, H), (60, 30, 20), -1)
    return img


def text(img, s, x, y, scale=2.0, color=(255, 255, 255), th=3):
    cv2.putText(img, s, (x, y), cv2.FONT_HERSHEY_DUPLEX, scale, color, th, cv2.LINE_AA)


def vs_frame(p1, p2):
    img = base()
    text(img, "VS", 590, 380, 2.5)
    text(img, p1, 120, 620)
    text(img, p2, 860, 620)
    return img


def play_frame():
    img = rng.integers(0, 255, (H, W, 3), dtype=np.uint8)
    return cv2.GaussianBlur(img, (31, 31), 0)


def result_frame(word, mr, both=False):
    img = base()
    text(img, word, 150, 220, 3.5, (0, 220, 255), 6)
    if both:
        text(img, "LOSE" if word == "WIN" else "WIN", 850, 220, 3.5, (0, 220, 255), 6)
    text(img, "MR", 150, 420, 1.5)
    text(img, str(mr), 260, 420, 1.5)
    return img


def run(det, frames, t):
    events = []
    for f in frames:
        e = det.process(f, t[0])
        if e:
            events.append(e)
        t[0] += 1.0
    return events


def test_flow():
    d = tempfile.mkdtemp()
    cfg, store = Config(d), Store(d)
    det = Detector(cfg, store, Library(d))
    t = [1000.0]
    ev = run(det, [play_frame()] * 2 + [vs_frame("RYU", "JURI")] * 3 + [play_frame()] * 5
             + [result_frame("WIN", 1523)] * 3 + [result_frame("WIN", 1531)] * 3 + [play_frame()] * 50, t)
    assert len(ev) == 1, ev
    m = store.matches()[0]
    assert (m["result"], m["my_char"], m["opp_char"]) == ("win", "ryu", "juri"), m
    assert (m["rating"], m["delta"]) == (1531, 8), m

    # second match: both words on screen, I am P1 (left) and lose; rating shows only the final value
    ev = run(det, [vs_frame("RYU", "CHUN-LI")] * 3 + [play_frame()] * 5
             + [result_frame("LOSE", 1522, both=True)] * 4 + [play_frame()] * 50, t)
    assert len(ev) == 1, ev
    m = store.matches()[1]
    assert (m["result"], m["opp_char"], m["rating"], m["delta"]) == ("lose", "chunli", 1522, -9), m

    # a single flickering frame must not count
    assert run(det, [result_frame("WIN", 1522)] + [play_frame()] * 5, t) == []

    # rematch without a VS screen keeps the characters
    ev = run(det, [result_frame("WIN", 1530)] * 4 + [play_frame()] * 50, t)
    assert len(ev) == 1 and store.matches()[2]["opp_char"] == "chunli" and store.matches()[2]["carried"]

    s = summarize(store.matches())
    assert (s["wins"], s["losses"], s["rating"], s["rating_start"], s["rating_diff"]) == (2, 1, 1530, 1523, 7), s
    assert s["streak"] == 1


def test_roi_and_unknown_character():
    d = tempfile.mkdtemp()
    cfg, store = Config(d), Store(d)
    cfg.update({"player": {"side": "p2"},
                "rois": {"p1_char": [0.05, 0.75, 0.4, 0.15], "p2_char": [0.6, 0.75, 0.4, 0.15],
                         "rating": [0.08, 0.5, 0.3, 0.12]}})
    lib = Library(d)
    det = Detector(cfg, store, lib)
    t = [5000.0]
    run(det, [vs_frame("XQZW", "KEN")] * 3 + [play_frame()] * 3 + [result_frame("WIN", 1700)] * 4 + [play_frame()] * 50, t)
    m = store.matches()[0]
    assert m["my_char"] == "ken" and m["opp_char"] is None and m["opp_pending"], m
    assert m["rating"] == 1700 and m["delta"] is None
    # naming the unknown picture fills the match and teaches the picture
    pid = m["opp_pending"]
    assert lib.label_pending(pid, "zangief")
    store.resolve_pending(pid, "zangief")
    assert store.matches()[0]["opp_char"] == "zangief"
    run(det, [vs_frame("XQZW", "KEN")] * 3 + [play_frame()] * 3 + [result_frame("LOSE", 1690)] * 4 + [play_frame()] * 50, t)
    m = store.matches()[1]
    assert (m["opp_char"], m["result"], m["delta"]) == ("zangief", "lose", -10), m


if __name__ == "__main__":
    test_flow()
    test_roi_and_unknown_character()
    print("ok")
