"""Settings: defaults, load/save, character roster."""
from __future__ import annotations

import copy
import json
import os
import threading

# ROI = [x, y, w, h] as fractions of the frame (0-1). None = search the whole frame.
DEFAULT = {
    "obs": {"host": "127.0.0.1", "port": 4455, "password": "", "source": ""},
    # width: picture used for reading text. look_width: small picture for the cheap colour checks that run
    # on every tick (the big one is only fetched when there is something to read).
    "capture": {"width": 1280, "look_width": 640, "interval": 1.5},
    "server": {"port": 17860},
    # names: my player name(s) as shown in game -> decides each match whether I am 1P or 2P.
    # chars: characters I use (second clue). side: used only when neither clue works.
    "player": {"names": [], "chars": [], "side": "p1", "rating_label": "auto"},  # rating_label: auto / MR / LP
    "display": {"char_lang": "ja"},
    "history": {"retention_hours": 0, "reset_on_character_change": True},
    "input_display": {},
    "rois": {"vs": None, "result": None, "rating": None, "p1_char": None, "p2_char": None,
             "p1_name": None, "p2_name": None},
    "signals": {
        "vs": {"roi": "vs", "keywords": ["VS"], "templates": []},
        "win": {"roi": "result", "keywords": ["WIN", "WINS", "WINNER", "VICTORY"], "templates": []},
        "lose": {"roi": "result", "keywords": ["LOSE", "LOSES", "LOSER", "DEFEAT"], "templates": []},
    },
    "detect": {
        # "sf6": the real game screens at their fixed places (16:9). "generic": only the ROIs,
        # keywords and templates set by hand above (other games, unusual layouts).
        "layout": "sf6",
        "confirm_ticks": 2,        # same result on N ticks in a row before a match is recorded
        "cooldown": 45.0,          # seconds after a result during which no new match is recorded
        "template_threshold": 0.80,
        "char_threshold": 0.80,
        "rating_min": 100,
        "rating_max": 99999,
        "carry_minutes": 10,       # rematch without a VS screen reuses the previous characters
    },
}

# id, English name, Japanese name, extra spellings the OCR may return
ROSTER = [
    ("ryu", "Ryu", "リュウ", []),
    ("luke", "Luke", "ルーク", []),
    ("jamie", "Jamie", "ジェイミー", []),
    ("chunli", "Chun-Li", "春麗", ["CHUNLI"]),
    ("guile", "Guile", "ガイル", []),
    ("kimberly", "Kimberly", "キンバリー", []),
    ("juri", "Juri", "ジュリ", []),
    ("ken", "Ken", "ケン", []),
    ("blanka", "Blanka", "ブランカ", []),
    ("dhalsim", "Dhalsim", "ダルシム", []),
    ("ehonda", "E.Honda", "E.本田", ["EHONDA", "HONDA"]),
    ("deejay", "Dee Jay", "ディージェイ", ["DEEJAY"]),
    ("manon", "Manon", "マノン", []),
    ("marisa", "Marisa", "マリーザ", []),
    ("jp", "JP", "JP", []),
    ("zangief", "Zangief", "ザンギエフ", []),
    ("lily", "Lily", "リリー", []),
    ("cammy", "Cammy", "キャミィ", []),
    ("rashid", "Rashid", "ラシード", []),
    ("aki", "A.K.I.", "A.K.I.", ["AKI"]),
    ("ed", "Ed", "エド", []),
    ("akuma", "Akuma", "豪鬼", ["GOUKI"]),
    ("bison", "M.Bison", "ベガ", ["MBISON", "BISON", "VEGA"]),
    ("terry", "Terry", "テリー", []),
    ("mai", "Mai", "舞", []),
    ("elena", "Elena", "エレナ", []),
    ("sagat", "Sagat", "サガット", []),
    ("cviper", "C.Viper", "C.ヴァイパー", ["CVIPER", "VIPER"]),
    ("alex", "Alex", "アレックス", []),
    ("ingrid", "Ingrid", "イングリッド", []),
]


def char_name(char_id, lang="ja"):
    if not char_id:
        return "?"
    for cid, en, ja, _ in ROSTER:
        if cid == char_id:
            return ja if lang == "ja" else en
    return str(char_id)


def _merge(base, over):
    for k, v in over.items():
        if isinstance(v, dict) and isinstance(base.get(k), dict):
            _merge(base[k], v)
        else:
            base[k] = v
    return base


class Config:
    def __init__(self, data_dir):
        self.data_dir = data_dir
        self.path = os.path.join(data_dir, "config.json")
        self.lock = threading.RLock()
        self.data = copy.deepcopy(DEFAULT)
        os.makedirs(data_dir, exist_ok=True)
        if os.path.exists(self.path):
            with open(self.path, encoding="utf-8") as f:
                _merge(self.data, json.load(f))

    def __getitem__(self, key):
        return self.data[key]

    def update(self, patch):
        with self.lock:
            _merge(self.data, patch)
            self.save()

    def save(self):
        with self.lock:
            tmp = self.path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(self.data, f, ensure_ascii=False, indent=2)
            os.replace(tmp, self.path)
