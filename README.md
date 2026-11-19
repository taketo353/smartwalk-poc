# SmartWalk PoC

金沢大学・角間坂を想定した1地点のデモです。路面温度と画像ラベルをPCで受け取り、参考危険度を学生向け画面と大学向け画面に表示します。AtomS3Rを接続すると端末でも同じ判定を確認できます。

## PCだけで動かす

Python 3.11以降を使います。追加パッケージは不要です。リポジトリのルートで起動します。

```sh
python server.py
```

- 学生向け [http://127.0.0.1:8000/student](http://127.0.0.1:8000/student)
- 大学向け [http://127.0.0.1:8000/admin](http://127.0.0.1:8000/admin)

両画面を開き、別のターミナルで以下を実行します。

```sh
python demo.py
```

「低リスク → 注意 → 高リスク（凍結疑い） → 判定不能（温度送信が20秒途絶） → 低リスク（復帰）」の順に表示が変わります。このデモはテスト温度と手動ラベルを使います。観測履歴は `data/smartwalk.sqlite3` に保存されます。空の履歴で始める場合は、起動時に `--db data/demo2.sqlite3` のように新しいファイル名を指定します。

## 4人の担当と合流

| 担当 | 作業場所 | 昼までに通すこと |
| --- | --- | --- |
| 実機 | [firmware](firmware/) | Wi-Fiで温度を送信し、判定の取得と表示を確認 |
| 画像 | [vision](vision/) | 手動ラベルをAPIに送信。続いてPCで画像推論を試す |
| API・判定 | [server.py](server.py) | 受信・判定・履歴保存をテストデータで確認 |
| Web | [web](web/) | 学生向けと大学向けの両画面で同じ危険度を表示 |

午前にテストデータで全体をつなぎ、午後に手元の機材に合わせて実測値やモデル出力を接続します。デモの最後に、温度送信だけを止めて判定不能になることと、送信再開で復帰することを確認します。使った入力の種類と各ケースの結果を記録します。

スマホやAtomS3Rをつなぐ場合は、同じLANから `python server.py --host 0.0.0.0` で起動したPCのLANアドレスにアクセスします。PCデモと実機を別々に確認すると温度値が混在しません。APIと画面の稼働範囲はデモ用LANです。

## API

地点IDは `kakuma-slope-01` 固定です。温度とライブ画像は受信から20秒で期限切れになり、保存画像のラベルは次の画像入力まで保持します。

| メソッドとパス | 内容 |
| --- | --- |
| `POST /observations` | `location_id`、`surface_temperature_c`（℃、欠測は `null`）、`temperature_source`（`sensor` / `demo`） |
| `POST /vision` | `location_id`、`image_id`、`label`（`dry` / `wet` / `snow` / `ice` / `unknown`）、`vision_source`（`model` / `manual`）、`image_source`（`saved` / `live`） |
| `GET /status` | 最新の入力と、`risk`（`low` / `caution` / `high` / `unknown`）、`reason`、`observed_at`（温度データの受信時刻、未受信は `null`） |
| `GET /history` | `items` に温度受信時点の判定を新しい順に最大10件格納 |

しきい値はデモ用です。凍結検出精度の評価には、路面の実測と正解ラベルが必要です。

## テスト

```sh
python -m unittest discover -s tests -v
```
