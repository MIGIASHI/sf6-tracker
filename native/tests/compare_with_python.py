"""Check that the C++ colour checks decide like sf6tracker/vision.py on the same pictures.

Builds tests/scene_check with g++ (or uses the path in SCENE_CHECK), makes synthetic SF6-like
pictures plus noisy variants, and compares both answers picture by picture.

    python native/tests/compare_with_python.py
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from sf6tracker import vision  # noqa: E402

W, H = 640, 360


def box(img, x0, y0, x1, y1, bgr):
    img[int(y0 * H):int(y1 * H), int(x0 * W):int(x1 * W)] = bgr


def pictures(rng):
    out = []
    base = rng.integers(0, 256, (H, W, 3), dtype=np.uint8)
    out.append(("noise", base))
    out.append(("black", np.zeros((H, W, 3), np.uint8)))
    out.append(("white", np.full((H, W, 3), 255, np.uint8)))
    for side in ("left", "right"):
        img = np.full((H, W, 3), 40, np.uint8)
        win, lose = ((0.430, 0.466), (0.534, 0.572)) if side == "left" else ((0.534, 0.572), (0.430, 0.466))
        box(img, win[0], 0.776, win[1], 0.810, (20, 20, 20))
        box(img, win[0] + 0.006, 0.785, win[0] + 0.020, 0.800, (250, 250, 250))  # WIN letters
        box(img, lose[0] + 0.006, 0.785, lose[0] + 0.020, 0.800, (150, 150, 150))  # grey LOSE
        out.append(("bar-" + side, img))
    for hue in (120, 130, 150, 170, 175):
        hsv = np.zeros((H, W, 3), np.uint8)
        hsv[...] = (hue, 200, 200)
        import cv2
        img = np.full((H, W, 3), 30, np.uint8)
        part = cv2.cvtColor(hsv, cv2.COLOR_HSV2BGR)
        img[16:44, 304:336] = part[16:44, 304:336]
        out.append(("timer-h%d" % hue, img))
    # Edge cases around the thresholds, with noise on top.
    for i in range(60):
        img = out[3 + (i % 7)][1].astype(np.int16)
        img = img + rng.integers(-60, 61, img.shape)
        out.append(("noisy-%d" % i, np.clip(img, 0, 255).astype(np.uint8)))
    return out


def main():
    exe = os.environ.get("SCENE_CHECK")
    tmp = tempfile.mkdtemp()
    if not exe:
        exe = os.path.join(tmp, "scene_check")
        subprocess.check_call(["g++", "-O2", "-std=c++17", "-o", exe,
                               os.path.join(HERE, "scene_check.cpp"), os.path.join(HERE, "..", "src", "scene.cpp")])
    pics = pictures(np.random.default_rng(6))
    paths = []
    for i, (_, img) in enumerate(pics):
        p = os.path.join(tmp, "%03d.raw" % i)
        bgra = np.dstack([img, np.full((H, W), 255, np.uint8)])
        bgra.tofile(p)
        paths.append(p)
    lines = subprocess.check_output([exe] + paths, text=True).split("\n")
    bad = 0
    hits = [0, 0]
    for (name, img), line in zip(pics, lines):
        cpp = tuple(int(x) for x in line.split())
        py = (int(vision.result_bar_likely(img)), int(vision.battle_hud_likely(img)))
        hits[0] += py[0]
        hits[1] += py[1]
        if cpp != py:
            bad += 1
            print("DIFFERENT", name, "c++", cpp, "python", py)
    print("%d pictures, %d different (python saw %d result bars, %d timers)" % (len(pics), bad, hits[0], hits[1]))
    return 1 if bad or not all(hits) else 0


if __name__ == "__main__":
    sys.exit(main())
