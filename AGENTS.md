# AGENTS.md — SmartWalk PoC を別のPC（Windows可）で動かす手順

このリポジトリを触る人・AIエージェント向けの実務メモです。仕様の背景は [SmartWalk.md](SmartWalk.md)、APIの入出力は [README.md](README.md)、1日の進め方は [docs/one-day-poc.md](docs/one-day-poc.md) を見てください。

ここに書いてあるのは「**実機（レンピン機）を USB でつないで、PC上でAPIと2画面を動かし、実測温度とカメラ判定を出すまで**」の再現手順です。Wi‑Fi は使いません。

## 1. 機材の構成（実測で確認済み）

| 部品 | 内容 | 備考 |
| --- | --- | --- |
| マイコン | AtomS3R-CAM（ESP32-S3-PICO-1-N8R8） | 8MB Flash / 8MB PSRAM |
| 赤外線センサー | MLX90640（I2C 0x33） | 32×24 の熱画像。路面温度はこの中央領域の平均 |
| 表示 | SSD1306 OLED（I2C 0x3C） | 危険度・温度・API結果を表示 |
| カメラ | GC0308（内蔵） | 640×480。**ピントは30〜40cm**。近すぎると真っ白にボケる |

### 重要：I2C のピンは Grove ではない

リポジトリ内の [firmware/SmartWalkCam/config.example.h](firmware/SmartWalkCam/config.example.h) は Grove ポート前提（SDA=2 / SCL=1）ですが、**この機体では Grove に何も応答しません**。I2C スキャンで確認した実配線は次のとおりです。

| バス | 応答 |
| --- | --- |
| Grove（GPIO2/1） | なし |
| **下側 GPIO7(SDA) / GPIO8(SCL)** | **0x33 = MLX90640、0x3C = OLED** |

→ `config.local.h` で `SMARTWALK_I2C_SDA 7` / `SMARTWALK_I2C_SCL 8` を指定します。ここが 2/1 のままだと起動ログに `MLX90640 not found` と出て、温度が `null` のままになります。

ピンが分からない機体では [firmware/I2CScan](firmware/I2CScan/I2CScan.ino) を焼くと、候補ピンを総当たりして応答アドレスを表示します（`0x33` が出れば赤外線センサーは生きています）。

## 2. 事前準備（初回だけ／ネットにつながる場所で）

1. **Python 3.11以降**を入れる。Windows はインストール画面で「Add python.exe to PATH」にチェック。
2. **pyserial** を入れる。

```bat
python -m pip install pyserial
```

3. **Arduino IDE 2 系**を入れ、ボードマネージャーで **esp32（Espressif）3.x** を追加する。
4. ライブラリマネージャーで次の3つを入れる（BusIO は依存で自動で入る）。

- Adafruit MLX90640
- Adafruit SSD1306
- Adafruit GFX Library

## 3. ファームウェアを書き込む

`firmware/SmartWalkCam/config.example.h` を同じフォルダーに **`config.local.h`** としてコピーし、下記の内容にする（このファイルは `.gitignore` 対象なので、各PCで作る）。

```c
#pragma once

// 0: USB（usb_bridge.py 経由・Wi-Fi不要） / 1: Wi-Fi
#define SMARTWALK_USE_WIFI 0

#define SMARTWALK_WIFI_SSID ""
#define SMARTWALK_WIFI_PASSWORD ""
#define SMARTWALK_API_URL "http://192.0.2.1:8000"

// 実測値。Grove(2/1) ではないので注意。
#define SMARTWALK_I2C_SDA 7
#define SMARTWALK_I2C_SCL 8
#define SMARTWALK_OLED_ADDR 0x3C

// この機体のボタンGPIOは未確認のため無効（USBの `snap` で代用できる）
#define SMARTWALK_BUTTON_PIN -1

#define SMARTWALK_ROI_COL_START 8
#define SMARTWALK_ROI_COL_END 23
#define SMARTWALK_ROI_ROW_START 6
#define SMARTWALK_ROI_ROW_END 17

#define SMARTWALK_SEND_VISION 1
```

Arduino IDE の設定（**間違えると起動しないので注意**）:

| 項目 | 値 |
| --- | --- |
| ボード | ESP32S3 Dev Module（または M5AtomS3 系） |
| USB CDC On Boot | **Enabled** |
| PSRAM | **OPI PSRAM** |
| Flash Size | 8MB |
| Flash Mode | QIO 80MHz |
| Partition Scheme | 8M with spiffs (3MB APP/1.5MB SPIFFS) |

`SmartWalkCam.ino` を開いて書き込み、**書き込み後はシリアルモニターを閉じる**（`usb_bridge.py` がポートを使うため）。

## 4. 起動する（毎回）

1. 本体を**データ通信できるUSBケーブル**でPCにつなぐ。
2. ターミナルを2つ使う。1つ目で API:

```bat
python server.py
```

3. 2つ目で USB 中継（`COM5` は環境に合わせる。分からなければ引数なしで実行すると一覧が出る）:

```bat
python -m pip install pyserial
python firmware\usb_bridge.py --port COM5
```

`start_demo.bat` をダブルクリックしても同じことができる（APIの起動とポート選択をまとめて行う）。

4. ブラウザで開く。

- 学生向け <http://127.0.0.1:8000/student>
- 大学向け <http://127.0.0.1:8000/admin>

5秒ごとに更新され、中継のターミナルに次のような行が出れば成功。

```text
16:10:40 路面 18.55℃ 画像 dry → low
```

## 5. 動作確認（APIを直接叩く）

```bat
curl http://127.0.0.1:8000/status
curl http://127.0.0.1:8000/history
```

見るポイント:

- `surface_temperature_c` が **数値**であること（`null` なら赤外線センサーが動いていない＝ピン設定を疑う）
- `temperature_source` が `sensor`
- `risk` が `low` / `caution` / `high` / `unknown` のいずれか
- 20秒以上データが来ないと `unknown`（理由「路面温度の観測が途絶えています」）になる

## 6. 中継のコマンド（本体に送られる）

| 入力 | 動作 |
| --- | --- |
| `snap` | 撮影してすぐ送信。画像と熱画像が `data/snapshots/` に保存される |
| `info` | いまのカメラ特徴量・I2Cクロック・各デバイスの認識状態を表示 |
| `cal dry` / `cal wet` / `cal ice` / `cal snow` | いま写っている面をその状態として校正 |
| `cal clear` | 校正を消して簡易ルールに戻す |

校正値は本体のNVSに保存され、**電源を切っても残る**（`cal clear` で消える）。

## 7. 校正のコツ（ここを外すと判定が暴れる）

- **カメラと赤外線が同じ場所を向いていること**。判定に使うのは**カメラ画像の中央50%**と**熱画像のROI中央**だけ。
- **距離は30〜40cm**。近すぎると画面が真っ白にボケて特徴量が取れない（`texture` が 0.01 未満になる）。
- **4ステップ（dry/wet/ice/snow）は同じ位置・同じ照明で行う**。途中で動かすと校正がずれる。
- 濡れは**水を薄く張って、照明の映り込みがレンズに返る角度**にすると差が出る（`brightness` と `highlight` が動く）。
- 白い紙・白い机は「積雪」と誤判定しやすい。多少ザラついた暗めの面が向いている。
- **室内では0℃以下まで下がらないので `high` は出にくい**。`high` を出すには氷を直接乗せて1〜2分待つ。

## 8. 既知の問題

| 症状 | 原因・対処 |
| --- | --- |
| `MLX90640 not found` ／ 温度が `null` | I2Cピンが Grove(2/1) のまま。7/8 にする（[firmware/I2CScan](firmware/I2CScan/I2CScan.ino) で確認できる） |
| 中継に `POST /vision ... -> -1` が20%前後出る | USB CDC の返事取りこぼし。**APIには届いていて画面表示は正しい**。実機OLEDの「API」欄だけ一時的にエラー表示になる既知の現象 |
| `no bridge` がOLEDに出る | `usb_bridge.py` が動いていない／ポートが違う／シリアルモニターがポートを掴んでいる |
| 中継に `壊れた要求を無視しました` | USB経由で行が欠けた場合。1行を組み立て直してから転送する実装なので、壊れた本文はAPIに送らない |
| 画像が真っ白・判定が `dry`/`wet` で暴れる | レンズが近すぎる。30〜40cm離す（ピント固定） |
| 画面が「判定不明」のまま | 温度が来ていない（センサー／ピン）か、20秒以上データが途絶えている |

## 9. ファイル構成（関係するところだけ）

```text
server.py                  APIのエントリポイント
api/server.py              HTTP API（判定・履歴・画面の配信）
web/                       学生向け・大学向けの画面
firmware/usb_bridge.py     実機 ⇄ API のUSB中継（要 pyserial）
firmware/start_demo.bat    Windows用の起動スクリプト
firmware/SmartWalkCam/     観測ノードのファームウェア（温度・画像・OLED）
firmware/I2CScan/          I2Cの配線チェック用（診断専用、常用しない）
data/snapshots/            snap で保存した画像と熱画像（git管理外）
data/smartwalk.sqlite3     観測履歴（git管理外）
```

## 10. テスト

```bat
python -m unittest discover -s tests -v
```
