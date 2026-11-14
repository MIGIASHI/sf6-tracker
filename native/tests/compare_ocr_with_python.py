"""Check that the C++ text reading reads what sf6tracker/vision.py (RapidOCR 1.4.4) reads.

    python native/tests/compare_ocr_with_python.py <ocr_check> <onnxruntime lib> <models dir>

Renders SF6-like text lines (result bar, ratings, names, character names), reads them with both,
and compares the texts. Small position differences are fine; the texts must match.
"""
import json
import os
import subprocess
import sys
import tempfile

import cv2
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from sf6tracker import vision  # noqa: E402

LINES = [
    ["WIN 1-0 LOSE"], ["LOSE 0-2 WIN"], ["18683LP"], ["1502 MR"], ["51497 LP  1502 MR"],
    ["JURI", "DHALSIM"], ["CHUN-LI"], ["PLAYER1"], ["PLAYER 2"], ["YOU"], ["ZANGIEF  19760 LP"],
    ["E.HONDA"], ["M.BISON  MR 1620"], ["WASURENE"], ["Round 2"], ["KEN", "RYU"],
]


def render(lines, rng):
    h, w = 120 + 70 * len(lines), 900
    bg = rng.integers(0, 60)
    img = np.full((h, w, 3), bg, np.uint8)
    for i, t in enumerate(lines):
        scale = 1.2 + rng.random() * 0.8
        color = (235, 235, 235) if i % 2 == 0 else (60, 200, 250)
        cv2.putText(img, t, (40 + int(rng.integers(0, 120)), 90 + 70 * i), cv2.FONT_HERSHEY_DUPLEX, scale, color, 2, cv2.LINE_AA)
    return img


def norm(t):
    return "".join(t.upper().split())


def main():
    exe, ort_lib, models = sys.argv[1:4]
    rng = np.random.default_rng(3)
    pics = [render(l, rng) for l in LINES]
    tmp = tempfile.mkdtemp()
    paths = []
    for i, img in enumerate(pics):
        p = os.path.join(tmp, "%02d.bgr" % i)
        with open(p, "wb") as f:
            f.write(np.array([img.shape[1], img.shape[0]], np.int32).tobytes())
            f.write(np.ascontiguousarray(img).tobytes())
        paths.append(p)
    out = subprocess.check_output([exe, ort_lib, os.path.join(models, "ch_PP-OCRv4_det_infer.onnx"),
                                   os.path.join(models, "ch_PP-OCRv4_rec_infer.onnx")] + paths, text=True)
    ocr = vision.Ocr()
    same = 0
    for lines, img, line in zip(LINES, pics, out.splitlines()):
        cpp = sorted(norm(it["text"]) for it in json.loads(line))
        py = sorted(norm(it["text"]) for it in ocr.read(img, None, top=16, scale=1.0))
        ok = cpp == py
        same += ok
        print("%-4s %-28s c++ %s | python %s" % ("ok" if ok else "DIFF", " / ".join(lines), cpp, py))
    print("%d of %d pictures read the same" % (same, len(LINES)))
    if ocr.error:
        print("python OCR error:", ocr.error)
        return 2
    return 0 if same == len(LINES) else 1


if __name__ == "__main__":
    sys.exit(main())
