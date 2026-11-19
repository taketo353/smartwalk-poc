import json
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from server import LOCATION, create_server


class TestDemo(unittest.TestCase):
    def setUp(self):
        self.now = 1000.0
        directory = self.enterContext(tempfile.TemporaryDirectory())
        self.server = create_server("127.0.0.1", 0, Path(directory) / "test.db", lambda: self.now)
        self.worker = threading.Thread(target=self.server.serve_forever)
        self.worker.start()
        self.addCleanup(self.worker.join)
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def request(self, path, data=None):
        body = None if data is None else json.dumps(data).encode()
        request = Request(
            f"http://127.0.0.1:{self.server.server_port}{path}",
            data=body,
            headers={"Content-Type": "application/json"},
        )
        with urlopen(request, timeout=5) as response:
            return json.load(response)

    def vision(self, label):
        return self.request("/vision", {
            "location_id": LOCATION, "image_id": f"{label}.jpg", "label": label,
            "vision_source": "manual", "image_source": "saved",
        })

    def temperature(self, value):
        return self.request("/observations", {
            "location_id": LOCATION, "surface_temperature_c": value,
            "temperature_source": "demo",
        })

    def test_demo_and_history(self):
        self.assertEqual(self.request("/status")["risk"], "unknown")
        for label, temperature, expected in (
            ("dry", 5, "low"), ("wet", 1, "caution"), ("ice", -1, "high")
        ):
            self.vision(label)
            self.temperature(temperature)
            result = self.request("/status")
            self.assertEqual(result["risk"], expected)
            self.assertEqual(result["temperature_source"], "demo")
            self.assertEqual(result["vision_source"], "manual")
            self.now += 5
        self.now += 15
        self.assertEqual(self.request("/status")["risk"], "unknown")
        self.vision("dry")
        self.temperature(5)
        self.assertEqual(self.request("/status")["risk"], "low")
        self.assertEqual(self.request("/history")["items"][0]["risk"], "low")

    def test_null_temperature_and_bad_type(self):
        self.vision("dry")
        self.temperature(None)
        self.assertEqual(self.request("/status")["risk"], "unknown")
        with self.assertRaises(HTTPError) as error:
            self.temperature(True)
        self.assertEqual(error.exception.code, 400)

    def test_live_image_expires_even_when_temperature_is_fresh(self):
        self.request("/vision", {
            "location_id": LOCATION, "image_id": "live-1", "label": "ice",
            "vision_source": "model", "image_source": "live",
        })
        self.temperature(-1)
        self.assertEqual(self.request("/status")["risk"], "high")
        self.now += 20
        self.temperature(-1)
        self.assertEqual(self.request("/status")["risk"], "unknown")
