# SmartWalkCam（観測ノード）

レンピン本体（AtomS3R-CAM、MLX90640、SSD1306 OLED、ボタン）で動かすファームウェアです。路面温度をAPIに送り、返ってきた危険度をOLEDに出し、カメラ画像を配信します。

## 書き込み

1. Arduino IDE でレンピンと同じボード設定を選びます。PSRAM は `OPI PSRAM` を有効にします（カメラ画像の変換に使います）。
2. ライブラリはレンピンと同じ Adafruit_MLX90640、Adafruit_SSD1306、Adafruit_GFX です。
3. `config.example.h` を同じフォルダーに `config.local.h` としてコピーし、次を設定します。
   - `SMARTWALK_WIFI_SSID`、`SMARTWALK_WIFI_PASSWORD`:2.4 GHz の Wi-Fi
   - `SMARTWALK_API_URL`:APIを動かすPCのLANアドレス（例 `http://192.168.1.20:8000`）。APIは `python server.py --host 0.0.0.0` で起動します
   - `SMARTWALK_I2C_SDA`、`SMARTWALK_I2C_SCL`、`SMARTWALK_BUTTON_PIN`:レンピンのスケッチと同じピン番号
4. 書き込んでシリアルモニター（115200 bps）を開くと、`WiFi connected: http://...` と観測ノードのアドレスが表示されます。OLEDの最下段にも出ます。

カメラの初期化に失敗する場合は、シリアルに `Camera init failed` と出ます。そのときはレンピンのスケッチのカメラ設定（`camera_config_t`）をそのまま `initCamera()` に移してください。

## デモの流れ

1. 本体を路面（またはタイル）に向けます。OLEDに路面温度と、APIから返った危険度（`low` / `caution` / `high` / `unknown`）が出ます。
2. ボタンを押すと撮影し、すぐに温度を送ります。シリアルモニターで `snap` と入力しても同じです。
3. 画像担当は `http://<観測ノードのIP>/snapshot.jpg` から画像を保存して分類します。

```sh
curl -o dry.jpg http://192.168.1.30/snapshot.jpg
```

## 表示と配信

OLEDの表示:

| 行 | 内容 |
| --- | --- |
| 1〜2 | `RISK` と危険度 |
| 3 | 路面温度（中央部の平均、カッコ内は最小〜最大） |
| 4 | API送信の結果と撮影枚数 |
| 5 | 観測ノードのIPアドレス |

| URL | 内容 |
| --- | --- |
| `/snapshot.jpg` | ボタンで最後に撮った画像（640×480） |
| `/capture.jpg` | その場で撮った画像 |
| `/thermal.json` | 熱画像32×24（行ごとに並んだ768個の値）と中央部の平均・最小・最大 |

路面温度として平均する範囲は `config.local.h` の `SMARTWALK_ROI_*` で変えられます。手で持つときは路面が視野の中央に来るように向けます。
