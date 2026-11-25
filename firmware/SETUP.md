# デモPCの準備（誰のPCでも）

観測ノード（レンピン本体）をUSBケーブルでPCにつなぎ、学生向け・大学向けの画面を表示するまでの手順です。本体へのプログラムの書き込みは済んでいる前提です。このPCにArduino IDEは要りません。Wi-Fiも使いません。

## 1. 事前に（インターネットにつながる場所で）

デモ会場でネットにつながらなくても動くように、次の3つを先に済ませます。

1. **Python 3.11以降をインストールする**。[python.org](https://www.python.org/downloads/) から入れます。Windowsではインストール画面の最初で「Add python.exe to PATH」にチェックを入れます。
2. **このリポジトリをダウンロードする**。Gitを使う場合は `git clone https://github.com/taketo353/smartwalk-poc.git`。使わない場合は [GitHubのページ](https://github.com/taketo353/smartwalk-poc) の「Code」→「Download ZIP」で保存し、展開します。
3. **pyserial を入れる**。ターミナル（WindowsはPowerShell）で次を実行します。

```sh
python -m pip install pyserial
```

## 2. 起動する

本体をUSBケーブルでPCにつないでから起動します。ケーブルは充電専用ではなく、データ通信ができるものを使います。

**Windows**:展開したフォルダーの `firmware` を開き、`start_demo.bat` をダブルクリックします。

**Mac**:ターミナルでリポジトリのフォルダーに移動し、次を実行します。

```sh
sh firmware/start_demo.sh
```

使えるポートの一覧が出るので、本体のポートを入力します。

| OS | ポートの例 | 見分け方 |
| --- | --- | --- |
| Windows | `COM5` | 本体を抜き差しして、増えたり消えたりするもの |
| Mac | `/dev/cu.usbmodem1101` | `usbmodem` を含むもの |

ポートが分かっている場合は、`firmware\start_demo.bat COM5` や `sh firmware/start_demo.sh /dev/cu.usbmodem1101` のように最初から指定できます。

APIが起動し、中継のターミナルに `路面 3.0℃ 画像 dry → low` のような行が5秒ごとに出れば動いています。Windowsでは学生向け画面が自動で開きます。開かない場合や大学向け画面は、ブラウザで次を開きます。

- 学生向け [http://127.0.0.1:8000/student](http://127.0.0.1:8000/student)
- 大学向け [http://127.0.0.1:8000/admin](http://127.0.0.1:8000/admin)

## 3. デモ中の操作

中継のターミナルに入力すると本体に届きます。

| 入力 | 動作 |
| --- | --- |
| `snap` | 撮影してすぐ送信（本体のボタンと同じ） |
| `cal dry`、`cal wet`、`cal ice` | いまカメラが見ているタイルで校正。デモの前に、同じ照明で1回ずつ行う |
| `info` | いまの反射の特徴量と判定を表示 |

撮った画像と熱画像は `data/snapshots/` に保存されます。校正と照明の置き方は [SmartWalkCam の README](SmartWalkCam/README.md#光の反射による路面状態の推定) を参照してください。

終了するときは、中継のターミナルで Ctrl+C を押します。WindowsではAPIの窓（「SmartWalk API」）も閉じます。

## うまくいかないとき

| 症状 | 確認すること |
| --- | --- |
| `Python が見つかりません` | Pythonを入れ直し、「Add python.exe to PATH」にチェックを入れる |
| ポートの一覧に本体が出ない | ケーブルがデータ通信対応か。別のUSB端子や別のケーブルを試す |
| `could not open port`、`アクセスが拒否されました` | Arduino IDEのシリアルモニターなど、ほかのソフトがポートを使っていないか |
| `Address already in use`、`[WinError 10048]` | 前に起動したAPIが残っている。APIの窓を閉じるか、PCを再起動する |
| 本体のOLEDに `no bridge` | 中継（`usb_bridge.py`）が動いているか。ポートが合っているか |
| 画面が `判定不明` のまま | 中継のターミナルに5秒ごとの行が出ているか。`Camera init failed` や `MLX90640 not found` が出ていないか |

## 本体のプログラムを書き換える場合

書き換えるPCにだけ Arduino IDE が必要です。手順は [SmartWalkCam の README](SmartWalkCam/README.md) にあります。
