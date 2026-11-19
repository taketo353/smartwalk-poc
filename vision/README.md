# 路面画像の分類と送信

固定したUSBカメラで同じ場所の路面を撮り、PCで画像を分類します。保存済みの写真でも試せます。結果は `POST /vision` に送り、乾燥・湿潤・積雪・氷・判定不能のいずれかを画面に表示します。

## 手動デモ

SmartWalk API を起動してから、リポジトリのルートで実行します。手動入力は Python の標準ライブラリだけで動きます。

```sh
python -m vision manual --image-id demo-dry --label dry
python -m vision manual --image-id demo-ice --label ice
```

手元の画像をデモに使う場合は、画像を見てラベルを選び、画像を識別できる名前を `--image-id` に指定します。ラベルは `dry`（乾燥）、`wet`（濡れ）、`snow`（積雪）、`ice`（凍結）、`unknown`（判定できない）のいずれかです。

保存画像には既定の `saved` を使い、その場で撮った画像には `--image-source live` を指定します。撮影はカメラアプリなどで行います。既定の送信先は `http://127.0.0.1:8000/vision` です。別のホストを使う場合は `--base-url http://192.168.1.10:8000` のように指定します。

API に送るのは地点 ID、画像 ID、ラベル、判定元（`manual` または `model`）、画像の取得元（`saved` または `live`）です。画像ファイルは手元に残ります。

## RSPNet-S を使う場合

このモードは、手元の画像を公式実装で分類してから結果を送ります。公式コード、重み、画像は各自で用意してください。

1. [RSPNet の公式リポジトリ](https://github.com/YLong-maker/RSPNet)を取得し、RSCD 用 RSPNet-S の[公開チェックポイント](https://github.com/YLong-maker/RSPNet/releases/download/weight/model_val_acc_89.71_epoch_97.pth)を保存します。
2. 路面が写った画像を用意します。保存画像を使う実行例では `road.jpg` とします。

クラス表は、[RSCD-1millionの公開ミラー](https://huggingface.co/datasets/rezzzq/RSCD-1million/tree/main/train) の学習フォルダー名を、公式の[学習コード](https://github.com/YLong-maker/RSPNet/blob/main/Classification/Train) と同じ `ImageFolder` の順序で並べています。別の学習済みチェックポイントを使う場合は、学習時の `class_indices.json` を `--class-indices` で指定してください。

Python 3.12 の仮想環境での実行例です。Linux と Windows 向けの CPU 版 PyTorch を使います。対応する組み合わせは[PyTorch のインストール手順](https://pytorch.org/get-started/previous-versions/)で確認できます。

```sh
git clone https://github.com/YLong-maker/RSPNet.git ../RSPNet
python -m pip install torch==2.10.0 torchvision==0.25.0 --index-url https://download.pytorch.org/whl/cpu
python -m pip install Pillow timm thop PyWavelets opencv-python-headless
python -m vision model --image ../road.jpg --rspnet-dir ../RSPNet --weights ../model_val_acc_89.71_epoch_97.pth
```

画像 ID は既定でファイル名です。`--image-id` で変更できます。[公式の評価コード](https://github.com/YLong-maker/RSPNet/blob/main/Classification/test)に合わせて、RGB 画像を 224×224 にリサイズして正規化します。最上位クラス名から路面状態を取り出し、`water` は `wet`、`fresh snow` と `melted snow` は `snow` に対応させます。対応が不明なクラス名は `unknown` になります。

## 固定カメラで路面を撮る

USBカメラをPCにつなぎ、観測したい路面が画面に収まる位置に固定します。路面温度センサを使う場合は、センサが測る場所も画角に入れてください。PCでAPIを起動して次のコマンドを実行すると、5秒ごとに撮影して判定結果を送ります。

```sh
python -m vision camera --device 0 --interval 5 --rspnet-dir ../RSPNet --weights ../model_val_acc_89.71_epoch_97.pth
```

- USBカメラが選ばれない場合は `--device 1` も試してください。`--once` を付けると1回だけ送信して終了します。連続観測は Ctrl+C で止めます。
- 撮影や分類に失敗した回は「判定不能」（`unknown`）を送ります。カメラモードでは次の撮影時に再試行します。保存画像で失敗した場合は送信後に終了します。
- カメラの結果には `image_source: live` が付き、APIは受信から20秒で期限切れとして扱います。カメラモードの画像IDにはUTCの観測時刻を含めます。APIが表示する観測時刻は温度データの受信時刻です。

## 動作確認

リポジトリのルートで `python -m unittest -q vision.test_vision` を実行します。公開RSCDの乾いた路面画像と公式の学習済み重みで、保存画像とOpenCV入力の両方から「乾燥」の結果がAPIに届くことを確認済みです。実際に設置したカメラで、路面が画角に収まることと撮影が続くことを確認してください。
