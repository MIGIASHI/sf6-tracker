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
- Windows の入力表示は XInput / DirectInput から直接コントローラーを読みます。ゲーム操作中もブラウザのフォーカスに依存せず、設定画面とブラウザソースに入力を渡します。DirectInput はバックグラウンド・非排他で取得します。
- 入力表示の設定画面では、上のボタンをドラッグして配置を変えられます。「＋ ボタンを追加」で最大32個まで増やせ、各ボタンの名前・割り当て・削除も変更できます。作成したURLに配置も保存されるので、OBSのブラウザソースに貼り直してください。既存の8ボタンのURLも利用できます。

Python 版との違い: 画像ファイルの読み込み（画面の調整タブのアップロード）と、汎用レイアウト（SF6 以外の画面）は未対応です。
Python 版と同時には動かせません（どちらもポート 17860 を使うため）。

## 入れ方

1. GitHub の Actions →「Native plugin」の最新の実行から `sf6-tracker-native-windows` をダウンロードして展開します。
2. OBSを終了し、中の **`obs-plugins` と `data`** を、使用しているOBS本体のフォルダ（通常 `C:\Program Files\obs-studio\`）にコピーします。ポータブル版は `bin`・`data`・`obs-plugins` があるフォルダがコピー先です。
3. `obs-plugins\64bit\sf6-tracker-native.dll` と `data\obs-plugins\sf6-tracker-native\web\input.html` があることを確認します。`obs-plugins` の中にさらに `obs-plugins` を入れないでください。
4. OBS を再起動し、SF6 のゲームキャプチャを右クリック →「フィルタ」→「エフェクトフィルタ」の「＋」→「SF6 Tracker」。

以前のProgramData形式を入れている場合は、そちらをバックアップして外し、二重配置を避けてください。設定・対戦履歴の移動は不要です。

OBS 31 以降の 64bit Windows 版向けです。CIでは公式OBS 32.2.2でDLLの読み込みとフィルター登録を検証します。

## 開発

- ビルドは GitHub Actions（`.github/workflows/native-plugin.yaml`）で行います。手元でビルドする場合は Visual Studio 2022 の C++ ツールと CMake 3.28 以上を入れ、ONNX Runtime 1.20.1 の Windows 版と RapidOCR 1.4.4 のモデルを用意して、`native` で `cmake --preset windows-x64 -DSF6_ORT_DIR=... -DSF6_MODELS_DIR=...` → `cmake --build --preset windows-x64 --config RelWithDebInfo`。
- Python 版と同じ答えを出すかを、次のテストで確かめます（CI でも毎回実行）: 色判定 `compare_with_python.py`、文字の判定 `compare_text_with_python.py`、文字認識 `compare_ocr_with_python.py`、試合の記録 `compare_detector_with_python.py`。
- 同梱物のライセンスは `data/licenses/THIRD_PARTY.txt` にあります。
- 土台は [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) です。OBS（libobs）は GPL-2.0 のため、このフォルダも GPL-2.0（`LICENSE`）です。

### HTTP サーバーの終了・再起動テスト

OBS や OCR のモデルなしで、通信中の停止と再起動を確認できます（CMake と C++17 コンパイラが必要）。リポジトリ直下から実行します。

```
cmake -S native/tests -B http-build
cmake --build http-build --config Release
python native/tests/test_http.py http-build/Release/http_check.exe
```

Linux では最後の実行ファイルを `http-build/http_check` にします。GitHub Actions でも Windows / Linux の両方で実行します。
