"""Play the same drawn SF6 matches through the Python tracker and the native one and compare
what each records (result, characters, side, rating before/after, change).

    python native/tests/compare_detector_with_python.py <replay> <onnxruntime lib> <models dir>

The screens are drawn at the real SF6 places (result bar, HUD corners, VS band), so the SF6
layout path of both trackers runs, including the colour checks and the text reading.
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from sf6tracker.config import Config  # noqa: E402
from sf6tracker.detector import Detector, Library  # noqa: E402
from sf6tracker.store import Store  # noqa: E402

W, H = 1280, 720
FONT = cv2.FONT_HERSHEY_DUPLEX


def put(img, s, x, y, scale, color=(240, 240, 240), th=2):
    cv2.putText(img, s, (int(x * W), int(y * H)), FONT, scale, color, th, cv2.LINE_AA)


def blank():
    return np.full((H, W, 3), (40, 28, 24), np.uint8)


def vs_screen(c1, c2, r1, r2, n1, n2):
    img = blank()
    put(img, c1, 0.06, 0.80, 1.3)
    put(img, c2, 0.70, 0.80, 1.3)
    put(img, n1, 0.06, 0.88, 0.8)
    put(img, n2, 0.70, 0.88, 0.8)
    put(img, r1, 0.01, 0.945, 0.8)
    put(img, r2, 0.86, 0.945, 0.8)
    return img


def battle(c1, c2, you, r1, r2, n1, n2, timer="99"):
    img = blank()
    cv2.rectangle(img, (int(0.475 * W), int(0.045 * H)), (int(0.525 * W), int(0.12 * H)), (200, 43, 200), -1)
    put(img, timer, 0.482, 0.105, 1.0, (255, 200, 255))
    for side, c, r, n in (("p1", c1, r1, n1), ("p2", c2, r2, n2)):
        x = 0.01 if side == "p1" else 0.81
        put(img, c, x, 0.05, 0.75)
        if you == side:
            put(img, "YOU", x + 0.1, 0.05, 0.6, (80, 230, 250))
        put(img, n, x, 0.085, 0.55)
        put(img, r, x, 0.12, 0.55)
    return img


def result(p1res, mine, rating, name, seat):
    img = blank()
    cv2.rectangle(img, (int(0.40 * W), int(0.765 * H)), (int(0.60 * W), int(0.818 * H)), (20, 20, 20), -1)
    left, right = ("WIN", "LOSE") if p1res == "win" else ("LOSE", "WIN")
    put(img, left, 0.432, 0.806, 0.8, (250, 250, 250) if left == "WIN" else (150, 150, 150))
    put(img, "1-0", 0.487, 0.806, 0.7, (200, 200, 200))
    put(img, right, 0.536, 0.806, 0.8, (250, 250, 250) if right == "WIN" else (150, 150, 150))
    x = 0.06 if mine == "p1" else 0.64
    put(img, name, x, 0.815, 0.8)
    put(img, rating, x + 0.17, 0.815, 0.8)
    put(img, "PLAYER" + seat[1], 0.01 if seat == "p1" else 0.87, 0.395, 0.8)
    return img


def scenario():
    """(seconds, picture) pairs: several matches with different sides, labels and endings."""
    out = []
    t = [1000.0]

    def hold(img, secs, step):
        for _ in range(int(secs / step)):
            out.append((t[0], img))
            t[0] += step

    menu = blank()
    put(menu, "RANKED MATCH", 0.4, 0.5, 1.2)
    hold(menu, 4, 1.5)
    # 1: I am 2P (YOU on the right), Juri vs Dhalsim, I lose, LP counts down 18723 -> 18683
    hold(vs_screen("JURI", "DHALSIM", "15020 LP", "18723 LP", "Rival", "WASURENE"), 4, 0.5)
    hold(battle("JURI", "DHALSIM", "p2", "15020LP", "18723LP", "Rival", "WASURENE"), 20, 0.5)
    for v in (18723, 18710, 18695, 18683, 18683, 18683, 18683):
        out.append((t[0], result("win", "p2", "%dLP" % v, "WASURENE", "p2")))
        t[0] += 0.4
    hold(menu, 6, 1.5)
    # 2: I am 1P, JP mirror, MR, win 1500 -> 1507
    hold(vs_screen("JP", "JP", "1500 MR", "1488 MR", "WASURENE", "Other"), 4, 0.5)
    hold(battle("JP", "JP", "p1", "1500 MR", "1488 MR", "WASURENE", "Other"), 20, 0.5)
    for v in (1500, 1503, 1507, 1507, 1507, 1507):
        out.append((t[0], result("win", "p1", "%d MR" % v, "WASURENE", "p1")))
        t[0] += 0.4
    hold(menu, 4, 1.5)
    # 3: rematch straight into battle (no VS screen), I lose 1507 -> 1499
    hold(battle("JP", "JP", "p1", "1507 MR", "1495 MR", "WASURENE", "Other"), 20, 0.5)
    for v in (1507, 1503, 1499, 1499, 1499, 1499):
        out.append((t[0], result("lose", "p1", "%d MR" % v, "WASURENE", "p1")))
        t[0] += 0.4
    hold(menu, 4, 1.5)
    return out


def run_python(frames, d):
    cfg, store = Config(d), Store(d)
    det = Detector(cfg, store, Library(d))
    for t, img in frames:
        det.process(img, t)
    return store.matches()


KEYS = ("result", "my_char", "opp_char", "side", "side_by", "rating_before", "rating", "delta", "rating_label")


def main():
    exe, ort_lib, models = sys.argv[1:4]
    frames = scenario()
    settings = {"player": {"names": ["WASURENE"]}}
    dp, dc = tempfile.mkdtemp(), tempfile.mkdtemp()
    for d in (dp, dc):
        with open(os.path.join(d, "config.json"), "w", encoding="utf-8") as f:
            json.dump(settings, f)
    path = os.path.join(dc, "frames.bin")
    with open(path, "wb") as f:
        for t, img in frames:
            f.write(struct.pack("<dii", t, W, H))
            f.write(np.ascontiguousarray(img).tobytes())
    subprocess.run([exe, ort_lib, models, dc, path], check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(dc, "history.json"), encoding="utf-8") as f:
        native = json.load(f)["matches"]
    py = run_python(frames, dp)
    print("python recorded %d matches, native %d" % (len(py), len(native)))
    bad = len(py) != len(native)
    for i, (a, b) in enumerate(zip(py, native)):
        pa = {k: a.get(k) for k in KEYS}
        pb = {k: b.get(k) for k in KEYS}
        same = pa == pb
        bad = bad or not same
        print("%d %s python %s" % (i + 1, "ok  " if same else "DIFF", pa))
        if not same:
            print("       native %s" % pb)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
