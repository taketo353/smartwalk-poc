@echo off
rem SmartWalk demo launcher for Windows.
rem Double-click, or run from a terminal:  firmware\start_demo.bat COM5
rem Starts the API (server.py) in its own window and relays the observation node over USB.

chcp 65001 > nul
cd /d "%~dp0.."

where python > nul 2>&1
if errorlevel 1 (
  echo Python が見つかりません。https://www.python.org/downloads/ からインストールし、
  echo インストール画面で「Add python.exe to PATH」にチェックを入れてください。
  pause
  exit /b 1
)

python -c "import serial" > nul 2>&1
if errorlevel 1 (
  echo pyserial をインストールします...
  python -m pip install pyserial
)

set PORT=%1
if "%PORT%"=="" (
  python firmware\usb_bridge.py
  set /p PORT=観測ノードのポートを入力してください（例 COM5）:
)

start "SmartWalk API" python server.py
timeout /t 2 > nul
start "" http://127.0.0.1:8000/student
python firmware\usb_bridge.py --port %PORT%
pause
