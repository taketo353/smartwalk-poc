"""Relay the SmartWalkCam observation node to the SmartWalk API over a USB cable.

The node prints "@POST <path> <json>" on its serial port. This script sends the JSON to the
API on the PC and writes "@RESP <status> <json>" back, so no Wi-Fi is needed. Snapshots
("@JPEG <size>" followed by the bytes) and thermal frames ("@THERMAL <json>") are saved to
a folder. Lines typed here (snap, info, cal dry, ...) are passed to the node.

    python firmware/usb_bridge.py --port COM5
    python firmware/usb_bridge.py --port /dev/ttyACM0 --api http://127.0.0.1:8000

Requires pyserial (python -m pip install pyserial).
"""

import argparse
import json
import sys
import threading
from datetime import datetime
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial が必要です: python -m pip install pyserial")

ALLOWED_PATHS = ("/observations", "/vision")


def forward(api, path, body):
    """POST body to the API and return (status, compact single-line JSON)."""
    if path not in ALLOWED_PATHS:
        return 404, json.dumps({"error": "path not allowed"})
    request = Request(
        api.rstrip("/") + path,
        data=body.encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )
    try:
        with urlopen(request, timeout=3) as response:
            status, text = response.status, response.read().decode("utf-8")
    except HTTPError as error:
        status, text = error.code, error.read().decode("utf-8", "replace")
    except (URLError, OSError) as error:
        print(f"API に接続できません: {error}", flush=True)
        return -1, "{}"
    try:
        return status, json.dumps(json.loads(text), ensure_ascii=True, separators=(",", ":"))
    except ValueError:
        return status, "{}"


def save(out_dir, name, data, suffix):
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    path = out_dir / f"{name}-{stamp}{suffix}"
    path.write_bytes(data)
    (out_dir / f"latest-{name}{suffix}").write_bytes(data)
    return path


def relay(port, api, out_dir, lock):
    while True:
        line = port.readline()
        if not line:
            continue
        if line.startswith(b"@POST "):
            parts = line.decode("utf-8", "replace").strip().split(" ", 2)
            if len(parts) < 3:
                continue
            status, text = forward(api, parts[1], parts[2])
            with lock:
                port.write(f"@RESP {status} {text}\n".encode("ascii"))
            if parts[1] == "/observations" and status == 200:
                result = json.loads(text)
                print(
                    f"{datetime.now():%H:%M:%S} 路面 {result.get('surface_temperature_c')}℃ "
                    f"画像 {result.get('label')} → {result.get('risk')}",
                    flush=True,
                )
        elif line.startswith(b"@JPEG "):
            size = int(line.split()[1])
            data = port.read(size)
            port.readline()  # newline after the image
            if len(data) == size:
                print(f"画像を保存しました: {save(out_dir, 'snapshot', data, '.jpg')}", flush=True)
            else:
                print("画像の受信が途中で切れました", flush=True)
        elif line.startswith(b"@THERMAL "):
            data = line[len(b"@THERMAL "):].strip()
            print(f"熱画像を保存しました: {save(out_dir, 'thermal', data, '.json')}", flush=True)
        else:
            print(line.decode("utf-8", "replace").rstrip(), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="観測ノードのシリアルポート（例 COM5、/dev/ttyACM0）")
    parser.add_argument("--api", default="http://127.0.0.1:8000", help="SmartWalk API の URL")
    parser.add_argument("--out", type=Path, default=Path("data/snapshots"), help="画像と熱画像の保存先")
    args = parser.parse_args()

    if not args.port:
        print("使えるシリアルポート:")
        for info in list_ports.comports():
            print(f"  {info.device}  {info.description}")
        sys.exit("--port でポートを指定してください")

    args.out.mkdir(parents=True, exist_ok=True)
    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 1
    port.rts = False  # keep the ESP32-S3 out of reset
    port.dtr = True
    port.open()
    print(f"{args.port} と {args.api} を中継します。snap / info / cal dry|wet|ice|snow|clear を入力できます。")

    lock = threading.Lock()
    threading.Thread(target=relay, args=(port, args.api, args.out, lock), daemon=True).start()
    try:
        for command in sys.stdin:
            with lock:
                port.write(command.strip().encode("utf-8") + b"\n")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
