--[[
SF6 Tracker launcher for OBS (Tools -> Scripts -> +).
Start / stop buttons in the script window, assignable hotkeys, and an optional
"start with OBS". The tracker is always stopped when OBS closes if this script started it.
The tracker folder defaults to the folder above this script; change it in the script settings if you moved it.
]]
obs = obslua

local folder = ""
local port = 17860
local started_by_us = false
local autostart = false
local hk_start, hk_stop = obs.OBS_INVALID_HOTKEY_ID, obs.OBS_INVALID_HOTKEY_ID

local function default_folder()
  local dir = script_path():gsub("[/\\]+$", "")
  return (dir:gsub("[/\\][^/\\]+$", ""))
end

local function win(path) return (path:gsub("/", "\\")) end

local function exists(path)
  local f = io.open(path, "rb")
  if f then f:close() return true end
  return false
end

local function running()
  -- curl.exe ships with Windows 10 and later
  local ok = os.execute(string.format('curl -s -o NUL --max-time 1 http://127.0.0.1:%d/api/status', port))
  return ok == 0 or ok == true
end

local function start_tracker()
  local root = win(folder ~= "" and folder or default_folder())
  local py = root .. "\\.venv\\Scripts\\pythonw.exe"
  if not exists(py) then
    obs.script_log(obs.LOG_WARNING, "SF6 Tracker: " .. py .. " がありません。先に start.bat を一度実行してください")
    return
  end
  if running() then
    obs.script_log(obs.LOG_INFO, "SF6 Tracker: すでに起動しています")
    return
  end
  -- pythonw = no console window
  os.execute(string.format('cd /d "%s" && start "" /b "%s" -m sf6tracker --port %d', root, py, port))
  started_by_us = true
  obs.script_log(obs.LOG_INFO, "SF6 Tracker: 起動しました (" .. root .. ")")
end

local function stop_tracker()
  os.execute(string.format('curl -s -o NUL --max-time 2 -X POST http://127.0.0.1:%d/api/shutdown', port))
  started_by_us = false
  obs.script_log(obs.LOG_INFO, "SF6 Tracker: 停止しました")
end

function script_description()
  return "<b>SF6 Tracker</b><br>下のボタンでトラッカーを起動・停止します（ホットキーも設定できます）。<br>" ..
         "ここから起動した場合は、OBS を閉じると自動で止まります。<br>" ..
         "フォルダを空にすると、このスクリプトの1つ上のフォルダを使います。"
end

function script_properties()
  local p = obs.obs_properties_create()
  obs.obs_properties_add_button(p, "start", "▶ トラッカーを起動", function() start_tracker() return false end)
  obs.obs_properties_add_button(p, "stop", "■ トラッカーを停止", function() stop_tracker() return false end)
  obs.obs_properties_add_bool(p, "autostart", "OBS の起動時に自動で起動する")
  obs.obs_properties_add_path(p, "folder", "トラッカーのフォルダ", obs.OBS_PATH_DIRECTORY, nil, nil)
  obs.obs_properties_add_int(p, "port", "ポート", 1024, 65535, 1)
  return p
end

function script_defaults(settings)
  obs.obs_data_set_default_int(settings, "port", 17860)
  obs.obs_data_set_default_bool(settings, "autostart", false)
end

function script_update(settings)
  folder = obs.obs_data_get_string(settings, "folder")
  port = obs.obs_data_get_int(settings, "port")
  autostart = obs.obs_data_get_bool(settings, "autostart")
end

function script_load(settings)
  script_update(settings)
  hk_start = obs.obs_hotkey_register_frontend("sf6tracker.start", "SF6 Tracker を起動", function(pressed) if pressed then start_tracker() end end)
  hk_stop = obs.obs_hotkey_register_frontend("sf6tracker.stop", "SF6 Tracker を停止", function(pressed) if pressed then stop_tracker() end end)
  for _, pair in ipairs({ { hk_start, "hk_start" }, { hk_stop, "hk_stop" } }) do
    local a = obs.obs_data_get_array(settings, pair[2])
    obs.obs_hotkey_load(pair[1], a)
    obs.obs_data_array_release(a)
  end
  if autostart then start_tracker() end
end

function script_save(settings)
  for _, pair in ipairs({ { hk_start, "hk_start" }, { hk_stop, "hk_stop" } }) do
    local a = obs.obs_hotkey_save(pair[1])
    obs.obs_data_set_array(settings, pair[2], a)
    obs.obs_data_array_release(a)
  end
end

function script_unload()
  if started_by_us then stop_tracker() end
end
