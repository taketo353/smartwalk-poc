import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch
from urllib.error import URLError

from vision.__main__ import main
from vision.rspnet import read_class_indices, surface_label


class VisionCliTests(unittest.TestCase):
    def test_manual_post_uses_api_contract(self):
        with patch("vision.__main__.urlopen") as request, contextlib.redirect_stdout(io.StringIO()):
            request.return_value.__enter__.return_value.read.return_value = b'{"ok": true}'
            status = main(["manual", "--image-id", "demo-ice", "--label", "ice"])

        self.assertEqual(status, 0)
        sent, = request.call_args.args
        self.assertEqual(sent.full_url, "http://127.0.0.1:8000/vision")
        self.assertEqual(sent.get_method(), "POST")
        self.assertEqual(sent.get_header("Content-type"), "application/json")
        self.assertEqual(json.loads(sent.data), {
            "location_id": "kakuma-slope-01",
            "image_id": "demo-ice",
            "label": "ice",
            "vision_source": "manual",
            "image_source": "saved",
        })

    def test_network_error_exits_with_failure(self):
        with patch("vision.__main__.urlopen", side_effect=URLError("connection refused")):
            with contextlib.redirect_stderr(io.StringIO()) as output:
                status = main(["manual", "--image-id", "demo", "--label", "unknown"])

        self.assertEqual(status, 1)
        self.assertIn("connection refused", output.getvalue())

    def test_model_uses_inference_result_and_live_metadata(self):
        with patch("vision.rspnet.predict_class", return_value="water-asphalt-severe") as predict:
            with patch("vision.__main__.urlopen") as request:
                request.return_value.__enter__.return_value.read.return_value = b"{}"
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    status = main([
                        "model", "--image", "road.jpg", "--rspnet-dir", "upstream",
                        "--weights", "weights.pth",
                        "--image-source", "live", "--base-url", "http://localhost:8080/",
                    ])

        self.assertEqual(status, 0)
        predict.assert_called_once()
        self.assertEqual(read_class_indices(predict.call_args.args[3])[9], "ice")
        sent, = request.call_args.args
        self.assertEqual(sent.full_url, "http://localhost:8080/vision")
        self.assertEqual(json.loads(sent.data), {
            "location_id": "kakuma-slope-01",
            "image_id": "road.jpg",
            "label": "wet",
            "vision_source": "model",
            "image_source": "live",
        })

    def test_camera_posts_live_model_result(self):
        payload, camera, predictor, status = self.run_camera_once((True, object()))

        self.assertEqual(status, 0)
        self.assertEqual(payload["label"], "ice")
        self.assertEqual(payload["vision_source"], "model")
        self.assertEqual(payload["image_source"], "live")
        self.assertTrue(payload["image_id"].startswith("camera-2-"))
        predictor.predict_rgb_array.assert_called_once()
        camera.release.assert_called_once()

    def test_camera_posts_unknown_when_frame_is_missing(self):
        payload, camera, predictor, status = self.run_camera_once((False, None))

        self.assertEqual(status, 1)
        self.assertEqual(payload["label"], "unknown")
        self.assertEqual(payload["image_source"], "live")
        predictor.predict_rgb_array.assert_not_called()
        camera.release.assert_called_once()

    def test_failed_saved_image_clears_previous_label(self):
        with (
            patch("vision.rspnet.predict_class", side_effect=OSError("bad image")),
            patch("vision.__main__.urlopen") as request,
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            request.return_value.__enter__.return_value.read.return_value = b"{}"
            status = main([
                "model", "--image", "bad.jpg", "--rspnet-dir", "upstream",
                "--weights", "weights.pth", "--class-indices", "class_indices.json",
            ])
        self.assertEqual(status, 1)
        sent, = request.call_args.args
        self.assertEqual(json.loads(sent.data)["label"], "unknown")

    def run_camera_once(self, frame):
        cv2 = MagicMock()
        cv2.error = OSError
        camera = cv2.VideoCapture.return_value
        camera.isOpened.return_value = True
        camera.read.return_value = frame
        with (
            patch.dict(sys.modules, {"cv2": cv2}),
            patch("vision.rspnet.RSPNetClassifier") as classifier,
            patch("vision.__main__.urlopen") as request,
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            request.return_value.__enter__.return_value.read.return_value = b"{}"
            predictor = classifier.return_value
            predictor.predict_rgb_array.return_value = "ice"
            status = main([
                "camera", "--device", "2", "--once", "--rspnet-dir", "upstream",
                "--weights", "weights.pth", "--class-indices", "class_indices.json",
            ])
        cv2.VideoCapture.assert_called_once_with(2)
        classifier.assert_called_once()
        sent, = request.call_args.args
        return json.loads(sent.data), camera, predictor, status


class RspNetTests(unittest.TestCase):
    def test_surface_labels_from_rscd_class_names(self):
        cases = {
            "dry-asphalt-slight": "dry",
            "wet_concrete_smooth": "wet",
            "water-mud": "wet",
            "fresh snow": "snow",
            "melted_snow": "snow",
            "ice": "ice",
            "concrete": "unknown",
            "wet-ice": "unknown",
        }
        for name, expected in cases.items():
            with self.subTest(name=name):
                self.assertEqual(surface_label(name), expected)

    def test_rejects_incomplete_class_indices(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "class_indices.json"
            path.write_text('{"0": "ice"}', encoding="utf-8")
            with self.assertRaises(ValueError):
                read_class_indices(path)


if __name__ == "__main__":
    unittest.main()
