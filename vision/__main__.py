import argparse
import json
import sys
from pathlib import Path
from pickle import UnpicklingError
from urllib.error import HTTPError
from urllib.request import Request, urlopen


LOCATION_ID = "kakuma-slope-01"
LABELS = ("dry", "wet", "snow", "ice", "unknown")


def parse_args(argv):
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--base-url", default="http://127.0.0.1:8000", help="API のベース URL")
    common.add_argument("--image-source", choices=("saved", "live"), default="saved")

    parser = argparse.ArgumentParser(description="路面ラベルを SmartWalk API に送信します。")
    modes = parser.add_subparsers(dest="mode", required=True)
    manual = modes.add_parser("manual", parents=[common], help="目視で選んだラベルを送信")
    manual.add_argument("--image-id", required=True)
    manual.add_argument("--label", choices=LABELS, required=True)

    model = modes.add_parser("model", parents=[common], help="RSPNet-S で画像を分類して送信")
    model.add_argument("--image", type=Path, required=True)
    model.add_argument("--image-id", help="省略時は画像のファイル名")
    model.add_argument("--rspnet-dir", type=Path, required=True, help="公式リポジトリのパス")
    model.add_argument("--weights", type=Path, required=True)
    model.add_argument("--class-indices", type=Path, required=True)

    args = parser.parse_args(argv)
    if args.image_id is not None and not args.image_id.strip():
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


def main(argv=None):
    args = parse_args(argv)
    try:
        if args.mode == "manual":
            label = args.label
            image_id = args.image_id
        else:
            from .rspnet import predict_class, surface_label

            class_name = predict_class(args.image, args.rspnet_dir, args.weights, args.class_indices)
            label = surface_label(class_name)
            image_id = args.image_id or args.image.name
            print(f"RSPNet-S: {class_name} → {label}", file=sys.stderr)

        response = post_vision(args.base_url, {
            "location_id": LOCATION_ID,
            "image_id": image_id,
            "label": label,
            "vision_source": args.mode,
            "image_source": args.image_source,
        })
    except (OSError, ValueError, RuntimeError, ImportError, UnpicklingError) as error:
        print(f"vision: {error}", file=sys.stderr)
        return 1

    print(response)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
