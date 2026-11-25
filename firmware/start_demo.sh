#!/bin/sh
# SmartWalk demo launcher for macOS / Linux.
#   sh firmware/start_demo.sh /dev/cu.usbmodem1101
# Starts the API (server.py) in the background and relays the observation node over USB.
cd "$(dirname "$0")/.." || exit 1

PYTHON=python3
command -v "$PYTHON" >/dev/null 2>&1 || PYTHON=python
command -v "$PYTHON" >/dev/null 2>&1 || { echo "Python が見つかりません。https://www.python.org/downloads/ からインストールしてください。"; exit 1; }
"$PYTHON" -c "import serial" 2>/dev/null || "$PYTHON" -m pip install pyserial || exit 1

PORT="$1"
if [ -z "$PORT" ]; then
  "$PYTHON" firmware/usb_bridge.py
  printf "観測ノードのポートを入力してください: "
  read -r PORT
fi

"$PYTHON" server.py &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT INT TERM
sleep 2
echo "ブラウザで http://127.0.0.1:8000/student と http://127.0.0.1:8000/admin を開いてください。"
"$PYTHON" firmware/usb_bridge.py --port "$PORT"
