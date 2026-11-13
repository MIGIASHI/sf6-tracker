@echo off
cd /d "%~dp0"
set PY=python
where py >nul 2>nul && set PY=py -3
if not exist .venv\Scripts\python.exe (
  echo First run: installing packages. This takes a few minutes...
  %PY% -m venv .venv || goto err
  .venv\Scripts\python -m pip install --upgrade pip
  .venv\Scripts\python -m pip install -r requirements.txt || goto err
)
.venv\Scripts\python -m sf6tracker %*
pause
exit /b
:err
echo Setup failed. Install Python 3.10 or newer from https://www.python.org/ and run this file again.
if exist .venv rmdir /s /q .venv
pause
