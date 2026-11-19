import argparse
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from pickle import UnpicklingError
from urllib.error import HTTPError
from urllib.request import Request, urlopen


LOCATION_ID = "kakuma-slope-01"
LABELS = ("dry", "wet", "snow", "ice", "unknown")
MODEL_ERRORS = (OSError, ValueError, RuntimeError, ImportError, UnpicklingError)


def parse_args(argv):
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--base-url", default="http://127.0.0.1:8000", help="API のベース URL")
    files = argparse.ArgumentParser(add_help=False, parents=[common])
    files.add_argument("--image-source", choices=("saved", "live"), default="saved")
    rspnet = argparse.ArgumentParser(add_help=False)
    rspnet.add_argument("--rspnet-dir", type=Path, required=True, help="公式リポジトリのパス")
    rspnet.add_argument("--weights", type=Path, required=True)
    rspnet.add_argument(
        "--class-indices", type=Path,
        default=Path(__file__).with_name("rscd_class_indices.json"),
        help="クラス番号と名前の対応表（既定値はRSCDの27クラス）",
    )

    parser = argparse.ArgumentParser(description="路面ラベルを SmartWalk API に送信します。")
    modes = parser.add_subparsers(dest="mode", required=True)
    manual = modes.add_parser("manual", parents=[files], help="目視で選んだラベルを送信")
    manual.add_argument("--image-id", required=True)
    manual.add_argument("--label", choices=LABELS, required=True)

    model = modes.add_parser("model", parents=[files, rspnet], help="RSPNet-S で画像を分類して送信")
    model.add_argument("--image", type=Path, required=True)
    model.add_argument("--image-id", help="省略時は画像のファイル名")
    camera = modes.add_parser("camera", parents=[common, rspnet], help="USBカメラから周期的に分類")
    camera.add_argument("--device", type=int, default=0, help="カメラ番号（既定値 0）")
    camera.add_argument("--interval", type=float, default=5, help="観測の間隔（秒、既定値 5）")
    camera.add_argument("--once", action="store_true", help="1回だけ取得・判定・送信")

    args = parser.parse_args(argv)
    if args.mode == "camera":
        if args.device < 0 or not 0 < args.interval < 20:
            parser.error("カメラ番号は0以上、間隔は0秒より大きく20秒未満にしてください")
    elif args.image_id is not None and not args.image_id.strip():
        parser.error("--image-id に空の値は指定できません")
    return args


def post_vision(base_url, payload):
    request = Request(
        f"{base_url.rstrip('/')}/vision",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urlopen(request, timeout=10) as response:
            return response.read().decode("utf-8")
    except HTTPError as error:
        with error:
            detail = error.read(1024).decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP {error.code} {error.reason} {detail}".rstrip()) from error


def publish(args, image_id, label, image_source):
    response = post_vision(args.base_url, {
        "location_id": LOCATION_ID,
        "image_id": image_id,
        "label": label,
        "vision_source": "model" if args.mode == "camera" else args.mode,
        "image_source": image_source,
    })
    print(response, flush=True)


def run_camera(args):
    import cv2
    from .rspnet import RSPNetClassifier, surface_label

    classifier = RSPNetClassifier(args.rspnet_dir, args.weights, args.class_indices)
    capture = cv2.VideoCapture(args.device)
    try:
        while True:
            start = time.monotonic()
            image_id = f"camera-{args.device}-{datetime.now(timezone.utc):%Y%m%dT%H%M%S.%fZ}"
            label = "unknown"
            try:
                if not capture.isOpened():
                    capture.open(args.device)
                if not capture.isOpened():
                    raise OSError(f"カメラ {args.device} を開けません")
                ok, frame = capture.read()
                if not ok or frame is None:
                    if not args.once:
                        capture.release()
                    raise OSError(f"カメラ {args.device} から画像を取得できません")
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                class_name = classifier.predict_rgb_array(rgb)
                label = surface_label(class_name)
                print(f"RSPNet-S: {class_name} → {label}", file=sys.stderr)
            except (cv2.error, OSError, ValueError, RuntimeError) as error:
                print(f"vision: {error}", file=sys.stderr)
            try:
                publish(args, image_id, label, "live")
            except (OSError, RuntimeError) as error:
                if args.once:
                    raise
                print(f"vision: {error}", file=sys.stderr)
            if args.once:
                return int(label == "unknown")
            time.sleep(max(0, args.interval - (time.monotonic() - start)))
    finally:
        capture.release()


def main(argv=None):
    args = parse_args(argv)
    try:
        if args.mode == "camera":
            return run_camera(args)
        if args.mode == "manual":
            publish(args, args.image_id, args.label, args.image_source)
            return 0

        from .rspnet import predict_class, surface_label

        image_id = args.image_id or args.image.name
        try:
            class_name = predict_class(args.image, args.rspnet_dir, args.weights, args.class_indices)
        except MODEL_ERRORS as error:
            print(f"vision: {error}", file=sys.stderr)
            publish(args, image_id, "unknown", args.image_source)
            return 1
        label = surface_label(class_name)
        print(f"RSPNet-S: {class_name} → {label}", file=sys.stderr)
        publish(args, image_id, label, args.image_source)
    except KeyboardInterrupt:
        return 130
    except MODEL_ERRORS as error:
        print(f"vision: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
