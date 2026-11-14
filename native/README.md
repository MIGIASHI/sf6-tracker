# SF6 Tracker ネイティブ版（OBS プラグイン）

OBS のフィルターとして動く C++ 版の試作です。WebSocket も Python も使わず、OBS の中でゲーム画面を見ます。
Python 版と同じく、読むのは OBS に映っている画面だけです。カプコンのサーバーや外部サイトには一切アクセスしません。

## いまできること

- ゲームキャプチャにフィルター「SF6 Tracker」を付けるだけで、Python 版と同じ認識が OBS の中で動きます（勝敗・キャラ・1P/2P・LP/MR とその増減）。
- 文字認識は Python 版と同じ PP-OCRv4 モデル・同じ手順です（ONNX Runtime を C++ から直接使います）。
- オーバーレイ・操作パネル・入力表示は Python 版と同じページを、同じアドレスでプラグインが出します。
  - 操作パネル: `http://127.0.0.1:17860/control`（カスタムブラウザドックに追加）
  - オーバーレイ: `http://127.0.0.1:17860/overlay`（ブラウザソースに追加。見た目の URL は「見た目」タブで作れます）
  - 入力表示: `http://127.0.0.1:17860/input`
- 設定と履歴は `%APPDATA%\obs-studio\plugin_config\sf6-tracker-native\`（`config.json` / `history.json`、Python 版と同じ形式）。
- バトル中は色だけを見て文字は読みません。読み取りは優先度を下げた別スレッドで 1 スレッドだけ使います。

Python 版との違い: 画像ファイルの読み込み（画面の調整タブのアップロード）と、汎用レイアウト（SF6 以外の画面）は未対応です。
Python 版と同時には動かせません（どちらもポート 17860 を使うため）。

## 入れ方

1. GitHub の Actions →「Native plugin」の最新の実行から `sf6-tracker-native-windows` をダウンロードして展開します。
2. 中の `sf6-tracker-native` フォルダを `C:\ProgramData\obs-studio\plugins\` にコピーします。
3. OBS を再起動し、SF6 のゲームキャプチャを右クリック →「フィルタ」→「＋」→「SF6 Tracker（ネイティブ試作）」。

OBS 31 以降（33 で確認予定）の 64bit Windows 版向けです。

## 開発

- ビルドは GitHub Actions（`.github/workflows/native-plugin.yaml`）で行います。手元でビルドする場合は Visual Studio 2022 の C++ ツールと CMake 3.28 以上を入れ、ONNX Runtime 1.20.1 の Windows 版と RapidOCR 1.4.4 のモデルを用意して、`native` で `cmake --preset windows-x64 -DSF6_ORT_DIR=... -DSF6_MODELS_DIR=...` → `cmake --build --preset windows-x64 --config RelWithDebInfo`。
- Python 版と同じ答えを出すかを、次のテストで確かめます（CI でも毎回実行）: 色判定 `compare_with_python.py`、文字の判定 `compare_text_with_python.py`、文字認識 `compare_ocr_with_python.py`、試合の記録 `compare_detector_with_python.py`。
- 同梱物のライセンスは `data/licenses/THIRD_PARTY.txt` にあります。
- 土台は [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) です。OBS（libobs）は GPL-2.0 のため、このフォルダも GPL-2.0（`LICENSE`）です。
