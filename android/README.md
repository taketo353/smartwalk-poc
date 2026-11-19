# SmartWalk Android版

学生向けの危険度と簡易マップ、大学向けの温度・画像判定・観測履歴をAndroidアプリで確認できます。PCのSmartWalk APIにつないで使います。

## Android Emulatorで試す

1. Android Studioで `android/` を開きます。ビルドにはJDK 17とAndroid SDK Platform 35が必要です。Android 7.0（API 24）以降に対応しています。
2. Device Managerで仮想端末（AVD）を作ります。Android 14（API 34）以降の画像を使い、アプリの実行先に選んで ▶ を押します。
3. エミュレータを動かしているPCで、リポジトリのルートから次のコマンドを実行します。

```sh
python server.py
```

4. アプリの **接続設定** に `http://10.0.2.2:8000` を入力して保存します。
5. PCの別のターミナルで `python demo.py --hold 10` を実行します。学生向け画面で「低い → 注意 → 高い → 判定不明 → 低い」の変化と、大学向け画面で観測履歴を確認できます。

ビルド済みAPKを使う場合は、起動中のAVDへ `adb install -r SmartWalk-Android-debug.apk` でインストールできます。開発エミュレータからPCのループバックへアクセスするには `10.0.2.2` を使います。 [Android Emulatorのネットワーク](https://developer.android.com/studio/run/emulator-networking)

## Android実機で試す

PC側で `python server.py --host 0.0.0.0` を実行し、スマホとPCを同じWi-Fiにつなぎます。アプリにはPCのLANのIPを含むURL（例 `http://192.168.1.20:8000`）を登録します。HTTPで設定できるのはLANのプライベートIPv4とローカルホストです。HTTPSのサーバーも登録できます。

つながらない場合はスマホのブラウザーから同じURLの末尾に `/status` を付けて開きます。開けなければ、PCのファイアウォールとWi-Fiの端末間通信を確認します。

## APKをビルドする

`android/` で次のコマンドを実行します。

```sh
./gradlew assembleDebug
```

Windowsは `gradlew.bat assembleDebug` を実行します。APKは `app/build/outputs/apk/debug/app-debug.apk` に生成されます。
