# Type2DK A/B → CoreS3: independent ranging over two UARTs

機体上の Type2DK-A/B が固定 Type2BP 群とそれぞれ測距し、結果を
**両方とも PIO_13 の送信専用 software UART** から出します。
CoreS3 の PORT A を **2本とも RX** として使い、共通の µs 時計で記録します。
2DK間通信、BLE、UWBユーザーデータ転送、加速度センサ取得は使いません。

## 現在の到達点

- **0.3.0: 1111・2222・3333・4444・5555・6666・7777の固定側7台に対応。**
- 対象は **Type2DK Rev.4.1 ×2 / Type2BP EVK Rev.4.1 ×1〜7**。CoreS3に7行×A/Bの距離・nLosを表示。
- A/Bは各1つの1対多セッション。AはMAC 0x8888・Ch5、Bは0x9999・Ch9。固定側はA/B用の2セッション。
- **A/B同時動作を優先する選択により、既存の1111〜6666も固定側ファームウェアの更新が必要です。**
  番号は維持しますが、旧`anchor1_controlee.hex`をそのまま使う互換版ではありません。
- 初回は追加2BPの**7777だけ**でA/Bの距離を確認し、その後1111〜6666を追加します。
- A/B同時UART受信は旧版で実機確認済み。**新しい無線構成・7台同時測距は実機未確認です。**
- 加速度センサ・BLE・2DK間通信は使用しません。USBの128行保持・再試行を継続します。

**[書き込み・配線・確認手順](docs/RANGING_SETUP.md)**

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

## 書き込み

**[CoreS3のブラウザ書き込みページ](https://temesotejam.github.io/type2dk-uwb-uart2/)**

同じページから `type2dk-uwb-uart2-0.3.0-ranging.zip` を取得します。
2DK-A/Bには `2dk_A_range_v0.3.0.bin` / `2dk_B_range_v0.3.0.bin`、
追加の2BPには `2bp_7777_anchor_v0.3.0.bin` を書き込みます。
1111〜6666も番号に対応した`2bp_XXXX_anchor_v0.3.0.bin`を用意しています。
これら固定側BINはType2BP用です。Type2DKには書き込めません。
UWB基板はDK6 Windows GUI「任意のBIN」、CoreS3は上のページを使用します。

旧selftest/BP1〜BP3版はGitHubの過去リリースに残しています。0.3.0のZIPには混ぜていません。
CoreS3のUSBログは115200 bps / 8N1 / フロー制御なし、UWB基板自身のUSBログは3,000,000 bpsです。

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

実測版は `--profile config/dual_7bp.json` を指定し `--self-test` を外します。
2DK SDKには提供元の対応する村田パッチを先に適用してください。

固定側は別の **Type2BP SR150 v04.08.01 SDKと、同梱の村田パッチ**を使用します。

```sh
python type2bp/build/prepare_sdk.py /path/to/SR150/uwbiot-top --patch /path/to/2bp_prebuild_v04.08.01.patch
python type2bp/build/build.py --sdk /path/to/SR150/uwbiot-top --gcc-bin /path/to/toolchain/bin --anchor 0x7777 --profile config/dual_7bp.json --out build/7777
```

`--anchor 0x1111`〜`0x7777`で番号を指定します。SR150新SDKのsession handleとSR040のsession IDは別APIとして処理します。
SDKとパッチ本体はこのリポジトリに含めません。
ファームウェアにはSDKの起動処理を含め加速度センサ初期化を入れません。

- [プロトコルと時刻の意味](docs/PROTOCOL.md)
- [試験手順と遅延の限界](docs/VALIDATION.md)
- [実装の出所](docs/PROVENANCE.md)
- [SDK・ランタイムのライセンス通知](type2dk/licenses/)

CoreS3 GPIOは [M5Stack公式ピン表](https://docs.m5stack.com/en/core/CoreS3)、UARTは
[ESP-IDF 4.4 UART](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32s3/api-reference/peripherals/uart.html) を参照。
