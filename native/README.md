# SF6 Tracker ネイティブ版（試作）

OBS のフィルターとして動く C++ 版の試作です。WebSocket も Python も使わず、OBS の中でゲーム画面を見ます。
Python 版と同じく、読むのは OBS に映っている画面だけです。カプコンのサーバーや外部サイトには一切アクセスしません。

## いまできること（第1段階）

- ゲームキャプチャにフィルター「SF6 Tracker」を付けると、画面を 640x360 に縮めて 0.25 秒ごとに見ます（間隔は変更可）。
- 文字は読まず、色だけで「バトル中」「リザルト画面」「その他」を見分けます（Python 版の `result_bar_likely` / `battle_hud_likely` と同じ判定）。
- 場面が変わると OBS のログに `scene: battle -> result` のように出ます。フィルターの設定画面にも今の場面と回数が出ます。
- 「今の画面を保存」で、フィルターが見ている画面を `%APPDATA%\obs-studio\plugin_config\sf6-tracker-native\frame.bmp` に保存します（位置合わせの確認用）。
- 判定は優先度を下げた別スレッドで動くので、配信の描画を待たせません。

まだ勝敗・キャラ・レートの読み取り（文字認識）や、オーバーレイの表示はありません。次の段階で足します。

## 入れ方

1. GitHub の Actions →「Native plugin」の最新の実行から `sf6-tracker-native-windows` をダウンロードして展開します。
2. 中の `sf6-tracker-native` フォルダを `C:\ProgramData\obs-studio\plugins\` にコピーします。
3. OBS を再起動し、SF6 のゲームキャプチャを右クリック →「フィルタ」→「＋」→「SF6 Tracker（ネイティブ試作）」。

OBS 31 以降（33 で確認予定）の 64bit Windows 版向けです。

## 開発

- ビルドは GitHub Actions（`.github/workflows/native-plugin.yaml`）で行います。手元でビルドする場合は Visual Studio 2022 の C++ ツールと CMake 3.28 以上を入れて、`native` で `cmake --preset windows-x64` → `cmake --build --preset windows-x64 --config RelWithDebInfo`。
- `python native/tests/compare_with_python.py` で、C++ の色判定が Python 版と同じ答えを出すかを確かめます。
- 土台は [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) です。OBS（libobs）は GPL-2.0 のため、このフォルダも GPL-2.0（`LICENSE`）です。
