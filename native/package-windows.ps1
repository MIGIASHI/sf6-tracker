param(
    [Parameter(Mandatory = $true)][string]$ReleaseDir,
    [Parameter(Mandatory = $true)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$pluginRoot = Join-Path $ReleaseDir 'sf6-tracker-native'
$pluginDll = Join-Path $pluginRoot 'bin\64bit\sf6-tracker-native.dll'
$pluginData = Join-Path $pluginRoot 'data'
if (!(Test-Path -LiteralPath $pluginDll) -or !(Test-Path -LiteralPath $pluginData)) {
    throw 'The installed plugin DLL and data folder are required.'
}
if (Test-Path -LiteralPath $OutputDir) {
    throw 'The package output directory already exists; use a new directory.'
}
$binaryDir = Join-Path $OutputDir 'obs-plugins\64bit'
$dataDir = Join-Path $OutputDir 'data\obs-plugins\sf6-tracker-native'
New-Item -ItemType Directory -Path $binaryDir,$dataDir | Out-Null
Copy-Item -LiteralPath $pluginDll -Destination $binaryDir
Get-ChildItem -LiteralPath $pluginData | Copy-Item -Destination $dataDir -Recurse
@'
SF6 Tracker for Windows x64
Made by WASURENE.

1. OBSを終了します。
2. このZIP内の「obs-plugins」と「data」を、使用しているOBS本体のフォルダにコピーします。
   通常: C:\Program Files\obs-studio\
   ポータブル版: bin / data / obs-plugins があるフォルダです。
3. 以下の配置になっていることを確認します。
   obs-plugins\64bit\sf6-tracker-native.dll
   data\obs-plugins\sf6-tracker-native\web\input.html
4. OBSを起動し、ゲームキャプチャの「フィルタ」→「エフェクトフィルタ」→「＋」→「SF6 Tracker」を選びます。

obs-pluginsの中にobs-pluginsを重ねて入れないでください。
以前のProgramData形式を入れている場合は、そちらをバックアップして外し、二重配置を避けてください。
更新前のDLLとdataフォルダはバックアップすると元に戻せます。設定・対戦履歴の移動は不要です。
Windows x64向けです。Windows ARM64 / macOS版OBSには使えません。

操作パネル: http://127.0.0.1:17860/control
入力表示: http://127.0.0.1:17860/input?setup=1
オーバーレイ: http://127.0.0.1:17860/overlay
'@ | Set-Content -LiteralPath (Join-Path $OutputDir 'README.txt') -Encoding utf8NoBOM
