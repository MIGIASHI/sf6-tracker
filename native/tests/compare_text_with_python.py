"""Check that the C++ text helpers (native/src/text.cpp) answer like sf6tracker/vision.py.

    python native/tests/compare_text_with_python.py [text_check]

Builds tests/text_check with g++ when no path is given.
"""
import json
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from sf6tracker import vision  # noqa: E402
from sf6tracker.config import ROSTER  # noqa: E402

WORDS = ["18683LP", "1502 MR", "MR 1620", "MR:1620", "LP：19760", "51497 LP 1502 MR", "1,502MR", "12,345 LP", "1234567LP",
         "-1500LP", "+25", "LP", "MR", "1500", "19,760", "O15OO", "1O5O", "MRS 1500", "1500MRX", "AMR 1500", "LP1500",
         "WIN", "LOSE", "WIN1-0 LOSE", "WIN 2-1 LOSE", "LOSE0-2WIN", "WINNER", "PLAYER1", "PLAYER 2", "player2", "PLAYER3",
         "YOU", "CDHALSIM", "CJP", "MJURI", "DHALSIN", "ZANGlEF", "ZANGEF", "CHUN-LI", "CHUNLI", "E.HONDA", "E.本田",
         "春麗", "ジュリ", "ベガ", "豪鬼", "Dee Jay", "DEEJAY", "A.K.I.", "AKI", "KEN", "KEM", "RYU", "C.VIPER", "C.ヴァイパー",
         "INGRID", "1NGRID", "MARISA", "MANON", "KIMBERLY", "KIMBERIY", "わすれね", "ワスレネ", "WASURENE", "Wasurene_",
         "wasurene123", "WASUREN", "ＷＡＳＵＲＥＮＥ", "ｗａｓｕｒｅｎｅ", "忘れね・", "名無し", "TERRY", "MAI", "舞", "ELENA",
         "SAGAT", "ALEX", "LILY", "CAMMY", "RASHID", "ED", "EDO", "AKUMA", "GOUKI", "BISON", "M.BISON", "VEGA", "", " ", "-",
         "1500 MR 18000 LP", "MR1500LP", "15OO MR", "100LP", "99LP", "999999 MR", "1000000MR"]


def items_from(rng, n):
    out = []
    for _ in range(n):
        t = rng.choice(WORDS)
        if rng.random() < 0.3:
            t = t + rng.choice([" ", "", "  "]) + rng.choice(WORDS)
        x, y = rng.random() * 0.9, rng.random() * 0.9
        w, h = 0.02 + rng.random() * 0.2, 0.02 + rng.random() * 0.05
        out.append({"text": t, "x": x, "y": y, "w": w, "h": h, "cx": x + w / 2, "cy": y + h / 2})
    return out


def py_answer(c):
    fn = c["fn"]
    if fn == "match_character":
        return vision.match_character(c["text"]) or ""
    if fn == "name_matches":
        return vision.name_matches(c["text"], c["names"])
    if fn == "player_seat":
        return vision.player_seat(c["text"]) or ""
    if fn == "tokens":
        return vision.tokens(c["text"])
    if fn == "find_ratings":
        return [[v, l] for v, l, _ in vision.find_ratings(c["items"], c["lo"], c["hi"])]
    if fn == "find_rating":
        return vision.find_rating(c["items"], c["label"], c["lo"], c["hi"])
    if fn == "p1_result":
        return vision.p1_result(c["items"]) or ""


def cases():
    rng = random.Random(7)
    names = [["WASURENE"], ["わすれね"], ["Wasurene", "ワスレネ"], ["ED"], ["ab"], [""]]
    texts = WORDS + [cid for cid, *_ in ROSTER] + [en.upper() for _, en, _, _ in ROSTER] + [ja for _, _, ja, _ in ROSTER]
    for t in texts:
        yield {"fn": "match_character", "text": t}
        yield {"fn": "player_seat", "text": t}
        yield {"fn": "tokens", "text": t}
        for n in names:
            yield {"fn": "name_matches", "text": t, "names": n}
    for _ in range(3000):
        items = items_from(rng, rng.randint(0, 4))
        yield {"fn": "find_ratings", "items": items, "lo": 100, "hi": 99999}
        yield {"fn": "find_rating", "items": items, "label": rng.choice(["", "MR", "LP"]), "lo": 100, "hi": 99999}
        yield {"fn": "p1_result", "items": items}
    # result bars at realistic places
    for _ in range(500):
        x = 0.40 + rng.random() * 0.2
        words = rng.choice([["WIN1-0 LOSE"], ["LOSE 0-1WIN"], ["WIN"], ["LOSE"], ["WIN", "LOSE"], ["1-0"]])
        items = []
        for i, w in enumerate(words):
            items.append({"text": w, "x": x + 0.05 * i, "y": 0.78, "w": 0.03 * len(w) / 4, "h": 0.03})
        yield {"fn": "p1_result", "items": items}


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else None
    if not exe:
        exe = os.path.join(tempfile.mkdtemp(), "text_check")
        subprocess.check_call(["g++", "-O1", "-std=c++17", "-I", os.path.join(HERE, "..", "third_party"), "-o", exe,
                               os.path.join(HERE, "text_check.cpp"), os.path.join(HERE, "..", "src", "text.cpp")])
    cs = list(cases())
    inp = "".join(json.dumps(c, ensure_ascii=False) + "\n" for c in cs)
    out = subprocess.run([exe], input=inp.encode("utf-8"), capture_output=True, check=True).stdout.decode("utf-8").splitlines()
    bad = 0
    for c, line in zip(cs, out):
        want = py_answer(c)
        got = json.loads(line)
        if got != want:
            bad += 1
            if bad <= 25:
                print("DIFFERENT", json.dumps(c, ensure_ascii=False)[:200], "c++", got, "python", want)
    print("%d cases, %d different" % (len(cs), bad))
    return 1 if bad or len(out) != len(cs) else 0


if __name__ == "__main__":
    sys.exit(main())
