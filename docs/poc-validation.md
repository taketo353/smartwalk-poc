# SmartWalk PoCの検証記録

## Android版

2026年11月19日に、コミット [`dec2e61`](https://github.com/taketo353/smartwalk-poc/commit/dec2e61) のアプリをAndroid 14（API 34）のエミュレータにインストールした。PC側のAPIとつなぎ、保存画像に対応する手動ラベルとテスト温度を送って確認した。

| 操作 | 確認できた表示 |
| --- | --- |
| `http://10.0.2.2:8000` を接続設定に登録 | 学生向け画面で角間坂の簡易マップを表示 |
| 氷・−1℃を送信 | 学生向け画面に「高い」と判定理由を表示。大学向け画面に温度、画像判定、観測履歴を表示 |
| 温度の送信を停止 | 20秒経過後の更新で「判定不明」に変化 |
| 乾燥・+5℃で送信を再開 | 「低い」に復帰 |

- [エミュレータの画面](evidence/android-avd.png)
- [検証に使ったAPK](evidence/SmartWalk-Android-debug.apk)（デバッグ署名、`com.smartwalk.poc`）
- [起動・ビルド手順](../android/README.md)

APKのSHA-256は `cb51b41b1d1170f70f7c103ee1dd70e73789e27ba36cb58e07e39050e61e82a9`。Android実機、AtomS3R-CAM、路面温度センサをつないだ検証は未実施。

再現するには、PCで `python server.py` を起動して、アプリに上記のURLを設定する。PCの別のターミナルで `python demo.py --hold 10` を実行すると、低い → 注意 → 高い → 判定不明 → 低いの順に変化する。

## 画像認識

RSPNet-Sの公開済み重みを使い、SmartWalkが使う路面ラベルに変換して比較した。車道のテスト画像はRSCD-1millionの `test_50k` から6状態を各6枚選んだ。歩道はWikimedia Commonsの公開写真を8枚選び、目視または撮影者の説明を参照した。 [RSPNet](https://github.com/YLong-maker/RSPNet) · [RSCD](https://thu-rsxd.com/rscd/)

| 入力 | 一致した枚数 | 氷の例で一致した枚数 | 詳細 |
| --- | ---: | ---: | --- |
| RSCDの車道画像 | 34 / 36 | 6 / 6 | [画像別の結果](evidence/rscd-predictions.csv) |
| 歩道写真の全体 | 2 / 8 | 0 / 2 | [画像別の結果](evidence/sidewalk-predictions.csv) |
| 同じ歩道写真で歩道部分を切り出した場合 | 3 / 8 | 0 / 2 | [切り出し範囲と結果](evidence/sidewalk-predictions.csv) |

車道で外れたのは、濡れを「乾燥」、融雪を「氷」とした各1枚。歩道を切り出した場合、氷の2枚は「湿潤」と「積雪」になった。この枚数と参照ラベルでは、角間坂の歩道での精度はまだ分からない。CSVには画像の出典と判定を残した。

角間坂の紅葉写真と角間キャンパス周辺の雪景色もAPIに通した。記事の掲載日で金沢の日平均気温を調べ、紅葉は12.0℃、雪景色は2.0℃をデモ入力とした。どちらも「湿潤・注意」だった。撮影時刻と現地の路面温度は不明。 [紅葉の写真](https://iijikanazawa.com/news/contributiondetail.php?cid=11057) · [雪景色の写真](https://iijikanazawa.com/news/contributiondetail.php?cid=5789) · [気象庁・2022年11月](https://www.data.jma.go.jp/stats/etrn/view/daily_s1.php?prec_no=56&block_no=47605&year=2022&month=11&day=&view=) · [気象庁・2018年12月](https://www.data.jma.go.jp/stats/etrn/view/daily_s1.php?prec_no=56&block_no=47605&year=2018&month=12&day=&view=)

## 現地検証までに確認すること

- Android実機とPCを同じネットワークにつなぎ、学生向け画面と大学向け画面の更新を確認する。
- AtomS3R-CAMの `/capture.jpg` から画像を取得し、MLX90640が測る場所とカメラの画角を合わせる。 [観測端末の設計](../firmware/DESIGN.md)
- 歩道の乾燥・湿潤・積雪・氷の写真を増やし、撮影場所ごとに分けて評価する。低温時の判定ルールも実測値と照らし合わせる。
