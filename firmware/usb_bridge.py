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


class LineReader:
    """Split the node's output on newlines without ever returning a partial line.

    pyserial's readline() gives up at its timeout and returns half a frame, which used to
    be forwarded to the API as broken JSON. Buffering the raw bytes keeps frames whole.
    """

    def __init__(self, port, limit=1 << 20):
        self.port = port
        self.limit = limit
        self.buffer = bytearray()

    def _fill(self):
        chunk = self.port.read(self.port.in_waiting or 1)
        if chunk:
            self.buffer.extend(chunk)
            if len(self.buffer) > self.limit:
                self.buffer.clear()
        return bool(chunk)

    def readline(self):
        while b"\n" not in self.buffer:
            self._fill()
        line, _, rest = self.buffer.partition(b"\n")
        self.buffer = bytearray(rest)
        return bytes(line) + b"\n"

    def read(self, size):
        while len(self.buffer) < size:
            self._fill()
        data = bytes(self.buffer[:size])
        self.buffer = bytearray(self.buffer[size:])
        return data


def relay(port, api, out_dir, lock):
    reader = LineReader(port)
    while True:
        line = reader.readline()
        if not line:
            continue
        if line.startswith(b"@POST "):
            parts = line.decode("utf-8", "replace").strip().split(" ", 2)
            if len(parts) < 3:
                print(f"不完全な要求を無視しました: {line[:48]!r}", flush=True)
                continue
            try:
                json.loads(parts[2])
            except ValueError:
                # The USB link occasionally drops bytes; never forward a broken body.
                print(f"壊れた要求を無視しました: {parts[1]}", flush=True)
                with lock:
                    # A leading newline survives losing the first byte of the frame.
                    port.write(b"\n@RESP 400 {}\n")
                continue
            status, text = forward(api, parts[1], parts[2])
            result = json.loads(text)
            # The node only reads risk; keep the reply below the ESP32-S3 USB RX queue size.
            risk = result.get("risk") if isinstance(result, dict) else None
            reply = json.dumps({"risk": risk}) if risk in ("low", "caution", "high", "unknown") else "{}"
            with lock:
                port.write(f"\n@RESP {status} {reply}\n".encode("ascii"))
            if parts[1] == "/observations" and status == 200:
                print(
                    f"{datetime.now():%H:%M:%S} 路面 {result.get('surface_temperature_c')}℃ "
                    f"画像 {result.get('label')} → {result.get('risk')}",
                    flush=True,
                )
        elif line.startswith((b"location_id", b'"location_id')):
            # A "@POST" frame whose first bytes were lost; skip it and let the next cycle resend.
            print(f"途切れた要求を無視しました: {line[:40]!r}", flush=True)
        elif line.startswith(b"@JPEG "):
            size = int(line.split()[1])
            data = reader.read(size)
            reader.readline()  # newline after the image
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
