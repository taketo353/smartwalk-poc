"""SmartWalk PoC server, using only the Python standard library."""

import argparse
import json
import mimetypes
import sqlite3
import time
from contextlib import closing
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

LOCATION = "kakuma-slope-01"
STALE_SECONDS = 20
WEB = Path(__file__).resolve().parents[1] / "web"
LABELS = ("dry", "wet", "snow", "ice", "unknown")


def validate(path, payload):
    if not isinstance(payload, dict) or payload.get("location_id") != LOCATION:
        raise ValueError("Invalid location_id")
    if path == "/observations":
        value = payload.get("surface_temperature_c")
        source = payload.get("temperature_source")
        if "surface_temperature_c" not in payload or (
            value is not None
            and (type(value) not in (int, float) or not -100 <= value <= 150)
        ):
            raise ValueError("Invalid surface_temperature_c")
        if source not in ("sensor", "demo"):
            raise ValueError("Invalid temperature_source")
        return {"surface_temperature_c": value, "temperature_source": source}

    image_id = payload.get("image_id")
    if not isinstance(image_id, str) or not image_id.strip() or len(image_id) > 200:
        raise ValueError("Invalid image_id")
    for field, allowed in (
        ("label", LABELS),
        ("vision_source", ("manual", "model")),
        ("image_source", ("saved", "live")),
    ):
        if payload.get(field) not in allowed:
            raise ValueError(f"Invalid {field}")
    return {key: payload[key] for key in ("image_id", "label", "vision_source", "image_source")}


def risk_for(temperature, label):
    if temperature is None or label in (None, "unknown"):
        return "unknown", "路面温度または画像分類を確認できません"
    if label == "ice" and temperature <= 0:
        return "high", "画像は氷、路面温度は0℃以下です"
    if label == "dry" and temperature > 2:
        return "low", "画像は乾燥、路面温度は2℃を超えています"
    return "caution", "画像分類と路面温度から注意が必要と判断しました"


def current_status(database, now):
    fields = {
        "surface_temperature_c": None,
        "temperature_source": None,
        "label": None,
        "image_id": None,
        "vision_source": None,
        "image_source": None,
    }
    timestamps = {}
    for kind, received_at, payload in database.execute(
        "SELECT kind, received_at, payload FROM latest"
    ):
        fields.update(json.loads(payload))
        timestamps[kind] = received_at

    observed = timestamps.get("/observations")
    risk, reason = risk_for(fields["surface_temperature_c"], fields["label"])
    if observed is not None and now - observed >= STALE_SECONDS:
        risk, reason = "unknown", "路面温度の観測が途絶えています"
    elif fields["image_source"] == "live" and now - timestamps["/vision"] >= STALE_SECONDS:
        risk, reason = "unknown", "ライブ画像の観測が途絶えています"

    return {
        "location_id": LOCATION,
        "risk": risk,
        "reason": reason,
        "observed_at": (
            datetime.fromtimestamp(observed, timezone.utc).isoformat()
            if observed is not None
            else None
        ),
        **fields,
    }


def create_server(host, port, database_path, clock=time.time):
    database_path = Path(database_path)
    database_path.parent.mkdir(parents=True, exist_ok=True)
    with closing(sqlite3.connect(database_path)) as database, database:
        database.executescript(
            """
            CREATE TABLE IF NOT EXISTS latest (
                kind TEXT PRIMARY KEY,
                received_at REAL NOT NULL,
                payload TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS history (
                id INTEGER PRIMARY KEY,
                payload TEXT NOT NULL
            );
            """
        )

    class Handler(BaseHTTPRequestHandler):
        def send_body(self, status, body, content_type):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def send_json(self, status, payload):
            body = json.dumps(payload, ensure_ascii=False, allow_nan=False).encode("utf-8")
            self.send_body(status, body, "application/json; charset=utf-8")

        def do_GET(self):
            path = unquote(urlsplit(self.path).path)
            if path in ("/status", "/history"):
                with closing(sqlite3.connect(database_path)) as database:
                    if path == "/status":
                        result = current_status(database, clock())
                    else:
                        entries = database.execute(
                            "SELECT payload FROM history ORDER BY id DESC LIMIT 10"
                        )
                        result = {"items": [json.loads(row[0]) for row in entries]}
                self.send_json(200, result)
                return

            if path in ("/", "/student", "/admin"):
                file = WEB / "index.html"
            elif path.startswith("/web/"):
                file = (WEB / path.removeprefix("/web/")).resolve()
                if not file.is_relative_to(WEB):
                    self.send_json(404, {"error": "Not found"})
                    return
            else:
                self.send_json(404, {"error": "Not found"})
                return

            if not file.is_file():
                self.send_json(404, {"error": "Not found"})
                return
            mime_type = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
            self.send_body(200, file.read_bytes(), mime_type)

        def do_POST(self):
            path = urlsplit(self.path).path
            if path not in ("/observations", "/vision"):
                self.send_json(404, {"error": "Not found"})
                return
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 8192:
                    raise ValueError("Invalid body length")
                payload = validate(path, json.loads(self.rfile.read(size)))
            except (ValueError, OverflowError) as error:
                self.send_json(400, {"error": str(error)})
                return

            with closing(sqlite3.connect(database_path)) as database, database:
                database.execute("BEGIN IMMEDIATE")
                received_at = clock()
                database.execute(
                    "INSERT OR REPLACE INTO latest VALUES (?, ?, ?)",
                    (path, received_at, json.dumps(payload, allow_nan=False)),
                )
                result = current_status(database, received_at)
                if path == "/observations":
                    database.execute(
                        "INSERT INTO history (payload) VALUES (?)",
                        (json.dumps(result, ensure_ascii=False),),
                    )
            self.send_json(200, result)

    return ThreadingHTTPServer((host, port), Handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8000, type=int)
    parser.add_argument("--db", type=Path, default=Path("data/smartwalk.sqlite3"))
    args = parser.parse_args()
    with create_server(args.host, args.port, args.db) as server:
        print(f"SmartWalk: http://{args.host}:{server.server_port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
