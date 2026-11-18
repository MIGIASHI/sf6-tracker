# SF6 Tracker ネイティブ版（OBS プラグイン）

OBS のフィルターとして動く C++ 版の試作です。WebSocket も Python も使わず、OBS の中でゲーム画面を見ます。
Python 版と同じく、読むのは OBS に映っている画面だけです。カプコンのサーバーや外部サイトには一切アクセスしません。

## いまできること

- ゲームキャプチャにフィルター「SF6 Tracker」を付けるだけで、Python 版と同じ認識が OBS の中で動きます（勝敗・キャラ・1P/2P・LP/MR とその増減）。
- 勝敗画面を取り逃した場合も、対戦中に確認した自分のレートと、次のVS画面・対戦HUDで3回続けて読めたレートを比べ、増加なら勝ち・減少なら負けとして補完します。記録には「レート変動から補完」のメモが付きます。使用キャラ・LP/MRの種類が変わった場合や、通常の勝敗判定で保存済みの場合は補完しません。レートが変わらない試合や、前後の数値を確認できない場合は補完できません。
- 文字認識は Python 版と同じ PP-OCRv4 モデル・同じ手順です（ONNX Runtime を C++ から直接使います）。
- オーバーレイ・操作パネル・入力表示は Python 版と同じページを、同じアドレスでプラグインが出します。
  - 操作パネル: `http://127.0.0.1:17860/control`（カスタムブラウザドックに追加）
  - オーバーレイ: `http://127.0.0.1:17860/overlay`（ブラウザソースに追加。見た目の URL は「見た目」タブで作れます）
  - 入力表示: `http://127.0.0.1:17860/input`
- 設定と履歴は `%APPDATA%\obs-studio\plugin_config\sf6-tracker-native\`（`config.json` / `history.json`、Python 版と同じ形式）。
- 設定で履歴の保持時間を変更できます（時間単位、0は無期限、0.5は30分）。件数上限はありません。期限切れは追加の対戦がなくても自動削除し、設定保存時・起動時にも適用します。バックアップは作成しません。戦績・レート推移・CSVも残った履歴が対象です。
- 自分の側とキャラが複数回確認できて、使用キャラが変わった場合は履歴・戦績・レート推移を自動リセットします。相手の変更や1P/2Pの入れ替わりではリセットしません。設定で自動リセットをオフにもできます。
- バトル中は主に色を見て判定し、取り逃し補完のため対戦HUDの文字も約5秒おきに確認します。レート変動を見つけたときだけ確認間隔を短くします。文字の読み取りは優先度を下げた別スレッドで1スレッドだけ使います。
- Windows の入力表示は XInput / DirectInput から直接コントローラーを読みます。ゲーム操作中もブラウザのフォーカスに依存せず、設定画面とブラウザソースに入力を渡します。DirectInput はバックグラウンド・非排他で取得します。
- 入力表示の設定画面では、上のボタンをドラッグして配置を変えられます。「＋ ボタンを追加」で最大32個まで増やせ、各ボタンの名前・割り当て・削除も変更できます。作成したURLに配置も保存されるので、OBSのブラウザソースに貼り直してください。既存の8ボタンのURLも利用できます。
- 入力表示の変更は自動保存され、再読み込み・OBS再起動後も復元されます。設定はトラッカーの `config.json` とブラウザ内に保存します。URLに個別設定がある場合はURLを優先します。
- パッドには左右アナログスティックの傾きとL3/R3押し込みを表示します。「キーボード・マウス」では指定したキー、左右・中央・戻る・進むのマウスボタン、ホイール上下・左右チルトを割り当てられます。表示対象の押下状態だけを読み、入力文字列やキー履歴をファイルには保存しません。ホイールは表示対象に選ばれている間だけ取得し、短時間点灯させます。

Python 版との違い: 画像ファイルの読み込み（画面の調整タブのアップロード）と、汎用レイアウト（SF6 以外の画面）は未対応です。
Python 版と同時には動かせません（どちらもポート 17860 を使うため）。

## 入れ方

配布ZIPは配置先に合わせて、どちらか一方を選んでください。中のDLLとモデルは同じで、どちらもエラー87の修正を含みます。

| ZIP / Actions成果物 | 配置先 |
|---|---|
| `sf6-tracker-native-windows` | OBS本体のフォルダ。中の `obs-plugins` と `data` をコピー |
| `sf6-tracker-native-windows-programdata` | 従来形式。中の `sf6-tracker-native` を `C:\ProgramData\obs-studio\plugins\` にコピー |

従来形式のDLLの配置は `C:\ProgramData\obs-studio\plugins\sf6-tracker-native\bin\64bit\sf6-tracker-native.dll` です。両形式を同時に入れないでください。

OBS本体のフォルダへ入れる場合の手順:

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
## ギルティギア ストライヴ

1. OBSでストライヴのゲームキャプチャを作り、「SF6 Tracker」フィルターを付けます。ゲーム画面全体が映るようにし、同時に映像を送るフィルターは1つにします。
2. 操作パネルの戦績タブ上部で「ギルティギア ストライヴ」を選び、設定タブでゲーム内のプレイヤーネームを登録します。オーバーレイには選択したゲームの戦績が出ます。
3. ランクマッチ後は、試合結果でRPまたはDRが表示されてから認識が終わるまで画面を残してください。認識した勝敗とレートはストライヴ専用の履歴に保存されます。

試合前のカードから自分と相手の使用キャラを読みます。名前が照合できない場合は「You」の表示を使います。設定でキャラ変更時のリセットが有効なら、識別できた自キャラが変わった次の記録時に、そのゲームの戦績と履歴がリセットされます。

レートが表示される結果画面が自動記録の対象です。認定戦などレートが出ない試合は操作パネルから手動で記録できます。

結果画面をスキップした場合は、次の対戦中に同じ自キャラのRPが変わったことを確認できれば、前の対戦を補完します。前後のRPとプレイヤー側を確認できた場合に動作し、RPの増減がゼロの場合は差分からの記録は行いません。
