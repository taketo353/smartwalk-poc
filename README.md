# SmartWalk PoC

**[SmartWalk 設計書（全文）](SmartWalk.md)** · [1日PoCの実施手順](docs/one-day-poc.md)

[Androidアプリの起動・検証手順](android/README.md)

金沢大学・角間坂を想定した1地点のデモです。路面温度と画像ラベルをPCで受け取り、参考危険度を学生向け画面と大学向け画面に表示します。AtomS3Rを接続すると端末でも同じ判定を確認できます。

## 4人で分担する

設計書を共有したうえで、自分の担当ブランチで作業します。担当ごとの作業場所は以下のとおりです。

| 担当 | ブランチ | 作業場所 | 最初に確認すること |
| --- | --- | --- | --- |
| 実機 | `poc/firmware` | [firmware/](firmware/) | Wi-Fiで温度を送り、端末で判定を表示できる |
| 画像 | `poc/vision` | [vision/](vision/) | 手動ラベルをAPIに送れる。続いてPCで画像推論を試す |
| API・判定 | `poc/api` | [api/](api/)・[tests/](tests/) | テストデータの受信、判定、履歴保存が動く |
| Web | `poc/web` | [web/](web/) | 学生向けと大学向けの画面に同じ判定が出る |

初回はリポジトリをクローンし、担当ブランチに切り替えます。実機担当の場合は次のとおりです。

```sh
git clone https://github.com/taketo353/smartwalk-poc.git
cd smartwalk-poc
git switch --track origin/poc/firmware
```

担当フォルダで変更をコミットしてpushし、`main` 向けのPRを作ります。複数担当にまたがるAPIの仕様変更は、関係する担当と先にすり合わせます。GitHubの書き込み権限がない場合はforkし、自分のforkからPRを作れます。

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

## デモの合流

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
