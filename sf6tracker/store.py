"""Match history kept in one JSON file."""
from __future__ import annotations

import csv
import io
import json
import os
import threading
import time

FIELDS = ("ts", "result", "my_char", "opp_char", "rating", "delta", "rating_label",
          "my_pending", "opp_pending", "auto", "carried", "note", "side", "side_by", "rating_before", "rushes")
HISTORY_LIMIT = 25


class Store:
    def __init__(self, data_dir):
        self.path = os.path.join(data_dir, "history.json")
        self.lock = threading.RLock()
        self.data = {"version": 1, "session": {"id": 1, "started": time.time()}, "next_id": 1, "matches": []}
        if os.path.exists(self.path):
            with open(self.path, encoding="utf-8") as f:
                self.data.update(json.load(f))
            if len(self.data["matches"]) > HISTORY_LIMIT:
                self._save()

    def _save(self):
        self.data["matches"].sort(key=lambda x: (x["ts"], x["id"]))
        self.data["matches"] = self.data["matches"][-HISTORY_LIMIT:]
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f, ensure_ascii=False, indent=1)
        os.replace(tmp, self.path)

    @property
    def session_id(self):
        return self.data["session"]["id"]

    def matches(self):
        with self.lock:
            return [dict(m) for m in self.data["matches"]]

    def get(self, match_id):
        with self.lock:
            for m in self.data["matches"]:
                if m["id"] == match_id:
                    return dict(m)
        return None

    def last(self):
        with self.lock:
            return dict(self.data["matches"][-1]) if self.data["matches"] else None

    def add(self, **fields):
        with self.lock:
            m = {"id": self.data["next_id"], "session": self.session_id, "ts": time.time(),
                 "result": "win", "my_char": None, "opp_char": None, "rating": None, "delta": None}
            m.update({k: v for k, v in fields.items() if k in FIELDS})
            self.data["next_id"] += 1
            self.data["matches"].append(m)
            self._save()
            return dict(m)

    def update(self, match_id, **fields):
        with self.lock:
            for m in self.data["matches"]:
                if m["id"] == match_id:
                    m.update({k: v for k, v in fields.items() if k in FIELDS})
                    self._save()
                    return dict(m)
        return None

    def delete(self, match_id):
        with self.lock:
            n = len(self.data["matches"])
            self.data["matches"] = [m for m in self.data["matches"] if m["id"] != match_id]
            self._save()
            return len(self.data["matches"]) < n

    def resolve_pending(self, pending_id, char_id):
        """A pending character picture got a name: fill it into every match that waited for it."""
        with self.lock:
            for m in self.data["matches"]:
                for side in ("my", "opp"):
                    if m.get(side + "_pending") == pending_id:
                        m[side + "_char"] = char_id
                        m[side + "_pending"] = None
            self._save()

    def new_session(self):
        with self.lock:
            self.data["session"] = {"id": self.session_id + 1, "started": time.time()}
            self._save()
            return self.session_id

    def csv(self):
        out = io.StringIO()
        w = csv.writer(out)
        w.writerow(["id", "datetime", "session", "result", "my_char", "opp_char", "rating_label", "rating", "delta", "note"])
        for m in self.matches():
            w.writerow([m["id"], time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(m["ts"])), m["session"],
                        m["result"], m.get("my_char") or "", m.get("opp_char") or "", m.get("rating_label") or "",
                        "" if m.get("rating") is None else m["rating"],
                        "" if m.get("delta") is None else m["delta"], m.get("note") or ""])
        return out.getvalue()


def summarize(matches):
    """Numbers for the overlay from a list of matches (oldest first)."""
    wins = sum(1 for m in matches if m["result"] == "win")
    losses = sum(1 for m in matches if m["result"] == "lose")
    streak = 0
    for m in reversed(matches):
        s = 1 if m["result"] == "win" else -1
        if streak == 0 or (streak > 0) == (s > 0):
            streak += s
        else:
            break
    rated = [m for m in matches if m.get("rating") is not None]
    series = [m["rating"] for m in rated]
    start = None
    if rated:
        first = rated[0]
        start = first["rating"] - first["delta"] if first.get("delta") is not None else first["rating"]
        if start != series[0]:
            series = [start] + series
    # Drive Rush counts exist only for matches played while the input display was counting
    rush = [m["rushes"] for m in matches if m.get("rushes") is not None]
    return {
        "rush_avg": round(sum(rush) / len(rush), 1) if rush else None,
        "rush_total": sum(rush), "rush_matches": len(rush),
        "wins": wins, "losses": losses, "total": wins + losses,
        "rate": round(100.0 * wins / (wins + losses), 1) if wins + losses else None,
        "streak": streak,
        "rating": series[-1] if series else None,
        "rating_start": start,
        "rating_diff": (series[-1] - start) if series else None,
        "series": series,
    }
