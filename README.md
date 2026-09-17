# Type2DK A/B → CoreS3: independent ranging over two UARTs

機体上の Type2DK-A/B が固定 Type2BP 群とそれぞれ測距し、結果を
**両方とも PIO_13 の送信専用 software UART** から出します。
CoreS3 の PORT A を **2本とも RX** として使い、共通の µs 時計で記録します。
2DK間通信、BLE、UWBユーザーデータ転送、加速度センサ取得は使いません。

## 現在の到達点

- 2DK: 測距通知ごとの48バイト送信、非同期キュー、結果取得・送信開始時刻、UCI連番、失敗・nLos raw。
- CoreS3: A/BそれぞれのハードウェアUART、受信割り込みでバイト時刻取得、CRC、欠落・再起動・接続先違い検出、画面とUSBログ。
- 最初の配布BINは **UART試験用**。画面の `UART TEST` は合成データで、UWB測距成功を意味しません。
- 固定2BPの現行プログラムとセッション設定は未取得です。例の設定は未確認として扱い、通常ビルドでは `CONFIG REQUIRED` を表示してUWBを開始しません。
- A/Bと複数2BPの同時測距、無線の時間配分、測距遅延の実測、位置姿勢推定は未検証です。

## 配線

| 送信元・端子 | CoreS3 PORT A |
|---|---|
| 2DK-A PIO_13 / TP8 pin 2 | 黄色 GPIO2 / UART1 RX |
| 2DK-B PIO_13 / TP8 pin 2 | 白色 GPIO1 / UART2 RX |
| 2DK-A/B GND（TP8 pin 3・5・9など） | 黒色 GND |
| 接続しない | 赤色 5V |

両2DKはそれぞれ従来のUSB給電を使用します。2DKの2本のTXを互いにつながないでください。
PIO_12は使用しません。以前の外部I²Cプルアップは外し、CoreS3の内部プルアップも無効にしています。
PORT AのI²Cは解放します。画面・タッチ用の内部I²Cは維持します。
SWDIOを転用するのでSWDデバッグとの同時使用はしません。

## 最初の確認

**[CoreS3のブラウザ書き込みページ](https://temesotejam.github.io/type2dk-uwb-uart2/)**

1. [Releases](https://github.com/temesotejam/type2dk-uwb-uart2/releases) の試験セットを取得。
2. 2DK-Aに `2dk_A_selftest_v0.1.0.bin`、Bに `2dk_B_selftest_v0.1.0.bin` を書き込みます。
   既存の [DK6 Windows GUI](https://temesotejam.github.io/type2dk-uwb-uart/tools/dk6-gui/type2dk-programmer-gui-v1.0.0.zip) の「任意のBIN」を使用できます。
3. PCのChrome/Edgeで上の書き込みページを開き、CoreS3をUSB接続して「CoreS3に書き込む」を押します。
   BINの選択や書き込みアドレスの入力は不要です。
4. 電源を入れ直し、A/B両方が `UART TEST`、受信数がそれぞれ増えることを確認します。
5. CoreS3のUSBログを取得します。旧2DKプログラムとはプロトコル非互換です。

試験用BINはUWBを起動せず、各2DKから約5件/秒の `type=TEST` を送ります。
**実距離は出ません。** `range_mm=-1`、画面の距離も `--` です。
2BPはこの配線試験には必要ありません。

## 実測へ進む際に必要な設定

`config/example_3bp.json` は設計例で、現在の2BP設定ではありません。
相手側のソース／設定を確認してMACアドレス、両端共通セッションID、役割、
チャンネル、SFD、プリアンブル、SP1/SP3、STS関連設定、周期を一致させます。
確認後 `confirmed_against_anchors: true` として通常版をビルドします。
現在の設定器は基本DS-TWR/unicast設定を対象とし、特殊STS鍵・セキュリティ設定は相手に応じた拡張が必要です。
各2BPにはA用とB用の測距設定が必要です。独立した局の開始オフセットだけで
共通スケジュールが保証されるわけではありません。

## ビルド・テスト

```sh
bash tests/run.sh
pip install platformio==6.1.18
pio run -e cores3_dual
```

2DKはNXP SDK `UWBIOT_SR040_v04.03.14_MCUx/uwbiot-top` と Arm GNU Toolchain が必要です。
SDKはこのリポジトリに含めません。SDKの作業コピーに次を適用します。

```sh
python type2dk/build/apply_sdk_changes.py /path/to/uwbiot-top
python type2dk/build/build.py --sdk /path/to/uwbiot-top --gcc-bin /path/to/toolchain/bin --node A --profile config/example_3bp.json --self-test --out build/A
python type2dk/build/build.py --sdk /path/to/uwbiot-top --gcc-bin /path/to/toolchain/bin --node B --profile config/example_3bp.json --self-test --out build/B
```

実測版は確認済みプロフィールを指定し `--self-test` を外します。
ファームウェアにはSDKの起動処理を含め加速度センサ初期化を入れません。

- [プロトコルと時刻の意味](docs/PROTOCOL.md)
- [試験手順と遅延の限界](docs/VALIDATION.md)
- [実装の出所](docs/PROVENANCE.md)
- [SDK・ランタイムのライセンス通知](type2dk/licenses/)

CoreS3 GPIOは [M5Stack公式ピン表](https://docs.m5stack.com/en/core/CoreS3)、UARTは
[ESP-IDF 4.4 UART](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32s3/api-reference/peripherals/uart.html) を参照。
