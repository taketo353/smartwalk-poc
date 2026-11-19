# 路面画像の判定を送る

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

このモードは、手元の画像を公式実装で分類してから結果を送ります。公式コード、重み、画像は各自で用意してください。公式の学習済み重みを使った通し実行は未確認です。

1. [RSPNet の公式リポジトリ](https://github.com/YLong-maker/RSPNet)を取得し、RSCD 用 RSPNet-S の[公開チェックポイント](https://github.com/YLong-maker/RSPNet/releases/download/weight/model_val_acc_89.71_epoch_97.pth)を保存します。
2. チェックポイントと同じ学習時の `class_indices.json` を用意します。`0`〜`26` のキーにクラス名が入った JSON です。公式の[学習コード](https://github.com/YLong-maker/RSPNet/blob/main/Classification/Train)は `ImageFolder` のクラス順をこの形式で保存します。
3. 路面が写った画像を用意します。保存画像を使う実行例では `road.jpg` とします。

学習時と同じ [RSCD](https://thu-rsxd.com/rscd/) のクラス別フォルダーが手元にある場合は、フォルダー名を名前順に並べて `class_indices.json` を作れます。以下の `../RSCD/train` は手元のパスに置き換えてください。

```sh
python -c "import json; from pathlib import Path; names = sorted(p.name for p in Path('../RSCD/train').iterdir() if p.is_dir()); assert len(names) == 27, names; Path('../class_indices.json').write_text(json.dumps(dict(enumerate(names)), indent=2), encoding='utf-8')"
```

Python 3.12 の仮想環境での実行例です。Linux と Windows 向けの CPU 版 PyTorch を使います。対応する組み合わせは[PyTorch のインストール手順](https://pytorch.org/get-started/previous-versions/)で確認できます。

```sh
git clone https://github.com/YLong-maker/RSPNet.git ../RSPNet
python -m pip install torch==2.10.0 torchvision==0.25.0 --index-url https://download.pytorch.org/whl/cpu
python -m pip install Pillow timm thop PyWavelets
python -m vision model --image ../road.jpg --rspnet-dir ../RSPNet --weights ../model_val_acc_89.71_epoch_97.pth --class-indices ../class_indices.json
```

画像 ID は既定でファイル名です。`--image-id` で変更できます。[公式の評価コード](https://github.com/YLong-maker/RSPNet/blob/main/Classification/test)に合わせて、RGB 画像を 224×224 にリサイズして正規化します。最上位クラス名から路面状態を取り出し、`water` は `wet`、`fresh snow` と `melted snow` は `snow` に対応させます。対応が不明なクラス名は `unknown` になります。
