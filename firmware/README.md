# AtomS3R ファームウェア

AtomS3R は約5秒ごとに `POST /observations` で路面温度を送信し、別の周期で `GET /status` から `risk` を取得します。観測地点は `kakuma-slope-01` です。起動時は青で、有効な `risk` を最後に受け取ってから20秒経つと青に戻ります。

| `risk` | 色 | LCD 表示 |
| --- | --- | --- |
| `low` | 緑 | `LOW` |
| `caution` | 黄 | `CAUTION` |
| `high` | 赤 | `HIGH` |
| `unknown` | 青 | `UNKNOWN` |

## 書き込み

1. Arduino IDE のボードマネージャーで M5Stack の ESP32 ボードパッケージを入れ、`M5AtomS3R` を選びます。ライブラリマネージャーで `M5GFX` 0.2.22 以降を入れます。ボード追加と書き込みモードの入り方は M5Stack の手順を参照してください。[AtomS3R の Arduino ガイド](https://docs.m5stack.com/en/arduino/m5atoms3r/program)
2. `SmartWalk/config.example.h` を同じフォルダーに `config.local.h` としてコピーします。`SMARTWALK_WIFI_SSID` と `SMARTWALK_WIFI_PASSWORD` に実機が接続する 2.4 GHz Wi-Fi の情報を入れます。`SMARTWALK_API_URL` には AtomS3R から届く API の URL を入れます。API を同じ LAN 上の PC で起動している場合は `http://192.168.1.20:8000` のように PC の LAN IP とポートを指定します。
3. AtomS3R を USB データケーブルで接続し、`SmartWalk/SmartWalk.ino` を開きます。シリアルポートを選んで書き込みます。`USB CDC On Boot` の設定が表示される場合は `Enabled` にします。
4. API を起動し、Arduino IDE のシリアルモニターを 115200 bps、改行ありに設定します。

本体だけで試すときは LCD に色が出ます。外付けの WS2812/NeoPixel を使う場合は、データ入力に接続した GPIO 番号を `SMARTWALK_RGB_PIN` に指定します。初期値の `-1` では外付け LED の出力を無効にします。[AtomS3R のピン配置](https://docs.m5stack.com/en/core/AtomS3R)

## デモ操作

起動時は `4.0 °C` を `temperature_source: "demo"` として送ります。以下のコマンドをシリアルモニターに1行ずつ入力してください。

| コマンド | 動作 |
| --- | --- |
| `temp 5`、`temp 1`、`temp -1` | 次回の観測から指定した温度を送信 |
| `temp missing` | 温度を `null` として送信 |
| `pause` | 観測の送信を一時停止。判定の取得は継続 |
| `resume` | 観測の送信を再開 |
| `sensor` | 温度取得をセンサー用の処理に切り替え |
| `help` | コマンド一覧を表示 |

シリアル出力に `Wi-Fi connected` と GET・POST の結果が表示されます。[画像ラベルの手動送信](../vision/README.md#手動デモ)で `dry`、`wet`、`ice` を送信し、それぞれ `temp 5`、`temp 1`、`temp -1` を入力すると緑、黄、赤を順に確認できます。

`pause` の後も GET が続くことを確認します。最後の温度観測から20秒経つと、次の GET で青に切り替わります。`resume` で観測を再開すると、判定色が復帰します。通信断を試すには判定色が出ている間に API を停止します。最後に判定を受け取ってから約20秒で青になります。API を再起動すると、次に取得した `risk` の色に切り替わります。

## 路面温度センサーの追加

センサーの機種が決まったら、機種に合わせて配線と初期化を行い、`readSurfaceTemperatureC()` で路面温度を °C で返します。読み取りに失敗した場合は `NAN` を返すと、API に `surface_temperature_c: null` を送ります。現状の `sensor` コマンドは、センサー未接続時の動作確認用に `temperature_source: "sensor"` と `surface_temperature_c: null` を送ります。`temp` でデモに戻せます。
