import json
import re
from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_file_location


CLASS_COUNT = 27
SURFACE_LABELS = {"dry": "dry", "wet": "wet", "water": "wet", "snow": "snow", "ice": "ice"}


def surface_label(class_name):
    words = set(re.findall(r"[a-z]+", class_name.casefold()))
    matched = {SURFACE_LABELS[word] for word in words & SURFACE_LABELS.keys()}
    if len(matched) == 1:
        return matched.pop()
    return "unknown"


def read_class_indices(path):
    classes = json.loads(path.read_text(encoding="utf-8-sig"))
    expected_keys = {str(index) for index in range(CLASS_COUNT)}
    if not isinstance(classes, dict) or classes.keys() != expected_keys:
        raise ValueError(f"{path} にクラス番号 0〜{CLASS_COUNT - 1} とクラス名を指定してください")
    names = [classes[str(index)] for index in range(CLASS_COUNT)]
    if any(not isinstance(name, str) or not name.strip() for name in names):
        raise ValueError(f"{path} のクラス名は空でない文字列にしてください")
    if len(set(names)) != CLASS_COUNT:
        raise ValueError(f"{path} に重複するクラス名があります")
    return names


class RSPNetClassifier:
    def __init__(self, rspnet_dir, weights_path, class_indices_path):
        self.classes = read_class_indices(class_indices_path)
        source = rspnet_dir / "Classification" / "RSPNet"
        for path in (source, weights_path):
            if not path.is_file():
                raise ValueError(f"ファイルが見つかりません: {path}")

        import torch
        from PIL import Image
        from torchvision import transforms

        # The upstream architecture is a Python file without a .py extension.
        loader = SourceFileLoader("smartwalk_upstream_rspnet", str(source))
        spec = spec_from_file_location(loader.name, source, loader=loader)
        module = module_from_spec(spec)
        loader.exec_module(module)
        self.model = module.RSPNet_S()
        checkpoint = torch.load(weights_path, map_location="cpu", weights_only=True)
        if not isinstance(checkpoint, dict):
            raise ValueError("RSPNet-S の state_dict またはそのチェックポイントを指定してください")
        self.model.load_state_dict(checkpoint.get("model_state_dict", checkpoint), strict=True)
        self.model.eval()
        self.torch = torch
        self.image = Image
        self.preprocess = transforms.Compose([
            transforms.Resize((224, 224)),
            transforms.ToTensor(),
            transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
        ])

    def predict_image(self, image_path):
        with self.image.open(image_path) as image:
            return self.predict(image.convert("RGB"))

    def predict_rgb_array(self, pixels):
        return self.predict(self.image.fromarray(pixels))

    def predict(self, image):
        pixels = self.preprocess(image).unsqueeze(0)
        with self.torch.inference_mode():
            scores = self.model(pixels)
            if scores.shape != (1, CLASS_COUNT) or not self.torch.isfinite(scores).all().item():
                raise ValueError("RSPNet-S の出力が無効です")
            return self.classes[scores.argmax(dim=1).item()]


def predict_class(image_path, rspnet_dir, weights_path, class_indices_path):
    if not image_path.is_file():
        raise ValueError(f"ファイルが見つかりません: {image_path}")
    return RSPNetClassifier(rspnet_dir, weights_path, class_indices_path).predict_image(image_path)
