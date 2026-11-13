from __future__ import annotations

import argparse
import os
import sys

from .app import App
from .server import serve


def main():
    ap = argparse.ArgumentParser(prog="sf6tracker", description="SF6 勝敗・MR トラッカー（OBS の画面だけを見ます）")
    ap.add_argument("--data", default=os.path.join(os.getcwd(), "data"), help="設定と履歴を置くフォルダ")
    ap.add_argument("--file", help="OBS の代わりに録画ファイル / 画像 / 画像フォルダを読む（調整用）")
    ap.add_argument("--port", type=int, help="ローカルサーバーのポート")
    args = ap.parse_args()

    app = App(args.data, args.file)
    port = args.port or int(app.cfg["server"]["port"])
    try:
        httpd = serve(app, port)
    except OSError as e:
        sys.exit("ポート %d を開けません: %s" % (port, e))
    app.start()
    print("SF6 Tracker 起動中")
    print("  操作パネル   : http://127.0.0.1:%d/control" % port)
    print("  オーバーレイ : http://127.0.0.1:%d/overlay" % port)
    print("止めるときは Ctrl+C")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    app.running = False


if __name__ == "__main__":
    main()
