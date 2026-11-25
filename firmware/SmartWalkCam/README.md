# SmartWalkCam（観測ノード）

レンピン本体（AtomS3R-CAM、MLX90640、SSD1306 OLED、ボタン）で動かすファームウェアです。路面温度と、カメラ画像の光の反射から推定した路面状態をAPIに送ります。どちらも学生向け画面（`/student`）と大学向け画面（`/admin`）にそのまま反映されます。返ってきた危険度はOLEDにも出します。

APIとのつなぎ方は2通りあります。既定はUSBです。

| 方式 | `SMARTWALK_USE_WIFI` | つなぎ方 |
| --- | --- | --- |
| USB（既定） | `0` | 本体とPCをUSBケーブルでつなぎ、PCで [usb_bridge.py](../usb_bridge.py) を動かす。Wi-Fiは使わない |
| Wi-Fi | `1` | 本体とPCを同じ2.4 GHzのWi-Fiにつなぐ。本体が画像をHTTPで配信する |

## USBで動かす（Wi-Fiなし）

APIは同じPCの中で動くので、ネットワークは要りません。

```text
本体 ──USBケーブル── PC（usb_bridge.py ── server.py ── ブラウザの /student と /admin）
```

1. Arduino IDE でレンピンと同じボード設定を選びます。`USB CDC On Boot` は `Enabled`、PSRAM は `OPI PSRAM` にします（カメラ画像の変換に使います）。
2. ライブラリはレンピンと同じ Adafruit_MLX90640、Adafruit_SSD1306、Adafruit_GFX です。
3. `config.example.h` を同じフォルダーに `config.local.h` としてコピーし、`SMARTWALK_I2C_SDA`、`SMARTWALK_I2C_SCL`、`SMARTWALK_BUTTON_PIN` をレンピンのスケッチと同じピン番号にします。`SMARTWALK_USE_WIFI` は `0` のままです。
4. 書き込んだら Arduino IDE のシリアルモニターを閉じます（ポートを `usb_bridge.py` が使うため）。
5. PCでリポジトリのルートから、APIと中継を別々のターミナルで起動します。中継には pyserial が必要です。

```sh
python server.py
```

```sh
python -m pip install pyserial
python firmware/usb_bridge.py --port COM5
```

`--port` を付けずに実行すると、使えるポートの一覧が出ます。Windows は `COM5` など、Mac は `/dev/cu.usbmodem...`、Linux は `/dev/ttyACM0` などです。

6. ブラウザで `http://127.0.0.1:8000/student` と `http://127.0.0.1:8000/admin` を開きます。5秒ごとに表示が更新されます。中継のターミナルには `路面 -1.25℃ 画像 ice → high` のように結果が出ます。

中継のターミナルに次のコマンドを入力すると本体に渡ります。

| コマンド | 動作 |
| --- | --- |
| `snap` | 撮影してすぐ送信。ボタンと同じ |
| `info` | いまの反射の特徴量と判定を表示 |
| `cal dry` など | いまの画像で校正（下記） |

ボタンか `snap` で撮った画像と熱画像は、PCの `data/snapshots/` に保存されます。最新のものは `latest-snapshot.jpg` と `latest-thermal.json` です。画像担当はこの画像を RSPNet に使えます。

## 光の反射による路面状態の推定

5秒ごとにカメラ画像の中央部を解析し、`POST /vision` に `label`（`dry` / `wet` / `ice` / `snow`）、`vision_source: "model"`、`image_source: "live"` で送ります。続けて路面温度を送るので、APIの判定（画像は氷かつ0℃以下なら高リスク、など）にすぐ使われます。

| 特徴量 | 見ているもの |
| --- | --- |
| `brightness` | 平均の明るさ。濡れたアスファルトは暗く、雪は明るい |
| `highlight` | 白飛びに近い画素の割合。水や氷の膜は鏡のように光を返し（鏡面反射）、ぎらつきが出る |
| `texture` | 隣り合う画素の明るさの差（明るさで割った値）。水や氷の膜はアスファルトのざらつきをならす |
| `saturation` | 色の鮮やかさ。雪は白いので低い |

### 校正（デモ前に必ず行う）

照明や路面の材質で特徴量が変わるので、デモで使う乾燥・濡れ・氷のタイルで校正します。校正するとその状態の特徴量を記録し、以後は最も近い状態を答えます。記録は電源を切っても残ります。

1. カメラを乾燥したタイルに向け、中継のターミナルで `cal dry` と入力します。
2. 濡れたタイルで `cal wet`、凍らせたタイルで `cal ice` と入力します。雪を使う場合は `cal snow`。
3. `info` で、いまの特徴量と判定を確認できます。やり直すときは `cal clear` で記録を消します。

校正しない場合は簡単なルールで判定します。ただし画像だけでは氷と水を見分けられないため、ぎらつくか滑らかな路面は `wet` と答え、`ice` は出しません。

### 照明の置き方

鏡面反射は、光源がカメラから見て路面の「鏡の位置」にあるときだけ見えます。カメラが斜め下を向くなら、タイルをはさんだ反対側の、カメラと同じくらいの高さにデスクライトを置きます。カメラのすぐ横から照らしても、反射光はカメラに戻りません。校正とデモは同じ照明の配置で行います。

### 画像担当との分担

RSPNet の結果も `POST /vision` に送られるため、両方を同時に動かすと後から届いた方で上書きされます。RSPNet で判定するときは `config.local.h` の `SMARTWALK_SEND_VISION` を `0` にします。

## OLEDの表示

| 行 | 内容 |
| --- | --- |
| 1〜2 | `RISK` と危険度（`low` / `caution` / `high` / `unknown`） |
| 3 | 路面温度（中央部の平均、カッコ内は最小〜最大） |
| 4 | 反射から推定した路面状態とAPI送信の結果（`OK`、`no bridge` など） |
| 5 | USBでは `USB bridge`、Wi-FiではIPアドレス |

`no bridge` は、PCの中継から返事がないことを示します。`usb_bridge.py` が動いているか、シリアルモニターがポートを使っていないかを確認します。

路面温度として平均する範囲は `config.local.h` の `SMARTWALK_ROI_*` で変えられます。手で持つときは路面が視野の中央に来るように向けます。

カメラの初期化に失敗すると、中継のターミナルに `Camera init failed` と出ます。そのときはレンピンのスケッチのカメラ設定（`camera_config_t`）をそのまま `initCamera()` に移します。

## Wi-Fiで動かす場合

`config.local.h` で `SMARTWALK_USE_WIFI` を `1` にし、`SMARTWALK_WIFI_SSID`、`SMARTWALK_WIFI_PASSWORD`、`SMARTWALK_API_URL`（APIを動かすPCのLANアドレス、例 `http://192.168.1.20:8000`）を設定します。APIは `python server.py --host 0.0.0.0` で起動します。スマホのテザリングを使うと、大学のWi-Fiにつなげない場合でも本体とPCを同じネットワークに置けます。

Wi-Fiでは、本体が次のURLで画像と測定値を配信します。コマンドはArduino IDEのシリアルモニターから入力します。

| URL | 内容 |
| --- | --- |
| `/snapshot.jpg` | ボタンで最後に撮った画像（640×480） |
| `/capture.jpg` | その場で撮った画像 |
| `/thermal.json` | 熱画像32×24（行ごとに並んだ768個の値）と中央部の平均・最小・最大 |
| `/reflection.json` | 反射の特徴量、判定結果、校正値 |
| `/calibrate?label=...` | いまの画像で校正（`dry` / `wet` / `ice` / `snow` / `clear`） |
