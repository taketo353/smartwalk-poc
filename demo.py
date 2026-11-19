"""Run the SmartWalk demo without an AtomS3R or a model."""

import argparse
import json
import time
from urllib.request import Request, urlopen

from server import LOCATION


def request(base_url, path, payload=None):
    body = json.dumps(payload).encode() if payload is not None else None
    request = Request(base_url + path, data=body, headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=5) as response:
        return json.load(response)


def run_demo(base_url, hold, interval=5):
    for label, temperature, expected in (
        ("dry", 5, "low"),
        ("wet", 1, "caution"),
        ("ice", -1, "high"),
        (None, None, "unknown"),
        ("dry", 5, "low"),
    ):
        if label is None:
            print("温度送信を停止します。20秒後に判定不能を確認します。", flush=True)
            time.sleep(20)
        else:
            request(base_url, "/vision", {
                "location_id": LOCATION,
                "image_id": f"demo-{label}",
                "image_source": "saved",
                "vision_source": "manual",
                "label": label,
            })
            for delay in range(hold, 0, -interval):
                request(base_url, "/observations", {
                    "location_id": LOCATION,
                    "surface_temperature_c": temperature,
                    "temperature_source": "demo",
                })
                time.sleep(min(delay, interval))
        status = request(base_url, "/status")
        print(f'{status["risk"]}: {status["reason"]}', flush=True)
        if status["risk"] != expected:
            raise SystemExit(f"期待した判定は {expected} です。")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://127.0.0.1:8000")
    parser.add_argument("--hold", type=int, default=8, help="各ケースの表示秒数")
    args = parser.parse_args()
    if args.hold < 1:
        parser.error("--hold must be at least 1")
    run_demo(args.base_url.rstrip("/"), args.hold)


if __name__ == "__main__":
    main()
