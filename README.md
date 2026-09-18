# Type2DK A/B → CoreS3: legacy-compatible 8-anchor ranging

Type2DK-A/Bが既存固定局1111〜6666を**書き換えず**に利用し、追加7777（Type2BP）と8888（Type2DK）を含む1111〜8888の距離を取得する実験版です。A/BのPIO13 UARTをCoreS3 PORT Aの2本で同時受信します。

## v0.4.0

- 移動側: Type2DK-A / B。ログ上の識別IDは **0050 / 0051**。
- 既存固定側 **1111〜6666はそのまま**。今回配布する固定側BINは7777と8888だけです。
- 7777: Type2BP EVK Rev.4.1、`2bp_7777_anchor_v0.4.0.bin`。
- 8888: Type2DK Rev.4.1、`2dk_8888_anchor_v0.4.0.bin`。
- CoreS3は1111〜8888の8行×A/Bを表示し、USBログには`tag_id=0050/0051`を付加します。
- 既存1111 HEX（SHA-256 `e53fa7e3...a05900d04`）を解析し、legacy controller `0x0000` / session `0x11223344` / Ch9 / 50 msに合わせています。
- **重要:** 旧固定局互換のためA/BはUWB上では同じcontroller address `0x0000`とsession IDを使用します。Bの開始を25 ms遅らせていますが、A/B同時RF動作はまだ実機未確認です。
- バイナリはQN9090 vector checksum、header CRC、boot trailerを検証済みです。無線測距の成功は実機確認が必要です。

## 配線

| 送信元 | CoreS3 PORT A |
|---|---|
| 2DK-A PIO13 / TP8 pin 2 | 黄色 GPIO2 / UART1 RX |
| 2DK-B PIO13 / TP8 pin 2 | 白色 GPIO1 / UART2 RX |
| A/B GND | 黒色 GND |
| 赤5V | 接続しない |

A/BはそれぞれUSB給電。UARTは38400 bps / 8N1です。

## 書き込み

**[CoreS3ブラウザ書き込みページ](https://temesotejam.github.io/type2dk-uwb-uart2/)**

ページのZIPはCoreS3用です。UWB基板のA/B・7777・8888 BINは、ユーザー提供のNXP/Murata SDKから別途ビルド・検証します。

- A → `2dk_A_range_v0.4.0.bin`
- B → `2dk_B_range_v0.4.0.bin`
- 7777 Type2BP → `2bp_7777_anchor_v0.4.0.bin`
- 8888 Type2DK → `2dk_8888_anchor_v0.4.0.bin`
- 1111〜6666 → **現在のファームを変更しない**

UWB基板は従来のDK6 Windows GUI「任意のBIN」で書き込み・照合してください。CoreS3だけブラウザページから更新します。

詳しい順序は[docs/RANGING_SETUP.md](docs/RANGING_SETUP.md)を参照してください。

## ソース・ビルド

無線設定の正本は`config/legacy_8anchor.json`です。NXP/Murata SDKとパッチはライセンス上このリポジトリに含めません。

```sh
bash tests/run.sh
pio run -e cores3_dual
```

Type2DKはSR040 v04.03.14、Type2BPはSR150 v04.08.01のユーザー提供SDKからビルドしています。
