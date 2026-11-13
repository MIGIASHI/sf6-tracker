"""Frame sources. The live one asks OBS (obs-websocket, built into OBS 28+) for a screenshot of a source."""
from __future__ import annotations

import base64
import glob
import os
import time

import cv2
import numpy as np


class ObsCapture:
    def __init__(self, config):
        self.cfg = config
        self.client = None
        self.key = None
        self.status = "未接続"
        self.retry_at = 0.0

    def _connect(self):
        o = self.cfg["obs"]
        key = (o["host"], o["port"], o["password"])
        if self.client is not None and key == self.key:
            return self.client
        self.close()
        if time.time() < self.retry_at:
            return None
        try:
            import obsws_python
            self.client = obsws_python.ReqClient(host=o["host"], port=int(o["port"]), password=o["password"], timeout=3)
            self.key = key
            self.status = "OBSに接続中"
        except Exception as e:
            self.client = None
            self.retry_at = time.time() + 5
            self.status = "OBSに接続できません（OBSのWebSocketサーバー設定とパスワードを確認）: %s" % (str(e) or type(e).__name__)
        return self.client

    def close(self):
        if self.client is not None:
            try:
                self.client.disconnect()
            except Exception:
                pass
        self.client = None

    def sources(self):
        cl = self._connect()
        if cl is None:
            return []
        out = []
        try:
            for i in cl.get_input_list().inputs:
                out.append({"name": i["inputName"], "kind": i.get("inputKind", "")})
            for s in cl.get_scene_list().scenes:
                out.append({"name": s["sceneName"], "kind": "scene"})
        except Exception as e:
            self.status = "OBSとの通信エラー: %s" % e
            self.close()
        return out

    def grab(self):
        cl = self._connect()
        if cl is None:
            return None
        source = self.cfg["obs"]["source"]
        if not source:
            self.status = "OBSに接続中。操作パネルの「設定」でSF6のソースを選んでください"
            return None
        try:
            w = int(self.cfg["capture"]["width"])
            resp = cl.get_source_screenshot(source, "jpg", w, w, 85)
            data = base64.b64decode(resp.image_data.split(",", 1)[1])
            frame = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
            if frame is not None:
                self.status = "キャプチャ中: %s" % source
            return frame
        except Exception as e:
            self.status = "ソース「%s」を取得できません: %s" % (source, str(e) or type(e).__name__)
            if "code 6" not in str(e):  # anything but "source not found" means the link is broken
                self.close()
                self.retry_at = time.time() + 3
            return None


class FileCapture:
    """Plays a recorded video or a folder of screenshots instead of OBS (for tuning and tests)."""

    def __init__(self, path, interval, width):
        self.status = "ファイル再生中: %s" % path
        self.width = width
        self.interval = interval
        self.t = 0.0
        self.video = None
        self.files = None
        self.done = False
        if os.path.isdir(path):
            self.files = sorted(f for f in glob.glob(os.path.join(path, "*")) if f.lower().endswith((".png", ".jpg", ".jpeg", ".bmp")))
        elif path.lower().endswith((".png", ".jpg", ".jpeg", ".bmp")):
            self.files = [path]
        else:
            self.video = cv2.VideoCapture(path)

    def sources(self):
        return []

    def grab(self):
        frame = None
        if self.video is not None:
            self.video.set(cv2.CAP_PROP_POS_MSEC, self.t * 1000)
            ok, frame = self.video.read()
            if not ok:
                frame = None
        elif self.files:
            i = int(round(self.t / self.interval))
            frame = cv2.imread(self.files[i]) if i < len(self.files) else None
        if frame is None:
            self.done = True
            self.status = "ファイルの最後まで再生しました"
            return None
        self.t += self.interval
        if frame.shape[1] > self.width:
            h = int(round(frame.shape[0] * self.width / frame.shape[1]))
            frame = cv2.resize(frame, (self.width, h), interpolation=cv2.INTER_AREA)
        return frame
