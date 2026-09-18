# 1111〜7777 / A+B 測距版 0.3.0

対象: **Type2DK Rev.4.1 ×2、Type2BP EVK Rev.4.1 ×1〜7、CoreS3**。
2DK-A/BはそれぞれPIO13 TXからCoreS3へ送信します。
A/Bの同時動作を優先する選択に合わせ、**固定側も新しいファームウェアへ更新する構成**です。
1111〜6666の番号は維持し、追加の2BPは7777にします。
旧固定側HEXおよび0.2.0のBP1〜BP3版との混在は対象外です。
この版はビルドとホスト試験済みで、実機の無線測距は未確認です。

## 書き込むファイル

| 機器 | BIN | 書き込み |
|---|---|---|
| 2DK-A | `2dk_A_range_v0.3.0.bin` | DK6 Windows GUI「任意のBIN」 |
| 2DK-B | `2dk_B_range_v0.3.0.bin` | 同上 |
| 追加の固定側2BP / 7777 | `2bp_7777_anchor_v0.3.0.bin` | 同じGUI、Type2BP側COM |
| 固定側1111 | `2bp_1111_anchor_v0.3.0.bin` | 同上 |
| 固定側2222 | `2bp_2222_anchor_v0.3.0.bin` | 同上 |
| 固定側3333 | `2bp_3333_anchor_v0.3.0.bin` | 同上 |
| 固定側4444 | `2bp_4444_anchor_v0.3.0.bin` | 同上 |
| 固定側5555 | `2bp_5555_anchor_v0.3.0.bin` | 同上 |
| 固定側6666 | `2bp_6666_anchor_v0.3.0.bin` | 同上 |
| CoreS3 | `cores3-dual-merged.bin` | ブラウザ書き込みページ |

固定側BINは**Type2BP EVK Rev.4.1用**です。Type2DK用とは交換できません。
これまでの[DK6 Windows GUI](https://temesotejam.github.io/type2dk-uwb-uart/tools/dk6-gui/type2dk-programmer-gui-v1.0.0.zip)で、
対象基板のCOMと「任意のBIN」を選び「書き込み・照合」を実行します。
対象COMを開いているTera Termを閉じ、基板を書き込みモードにしてから実行します。
書き込み後は各基板を再起動してください。2BPはUSB給電だけで起動します。
初回起動はSR150へのファームウェア転送・校正で時間がかかるため、30秒程度待ちます。
J-Linkを使う場合、2BPはRev.4.1のTP31・QN9090へ、BINをアドレス0から書き込みます。

## 配線

| 信号 | 接続先 |
|---|---|
| 2DK-A PIO13（TP8 pin 2） | CoreS3 PORT A 黄 GPIO2 |
| 2DK-B PIO13（TP8 pin 2） | CoreS3 PORT A 白 GPIO1 |
| 2DK-A/B GND | CoreS3 PORT A 黒 GND |
| PORT A 赤5V | 接続しない |

各2DKはUSB給電、以前のI²C外付けプルアップは取り外します。
UARTは両方38400 bps / 8N1。固定2BPとの有線通信はありません。
加速度センサ・BLE・2DK間通信・UWBユーザーデータ転送は使いません。

## 最初の実機確認

1. **7777の2BPを1台だけ**使い、2DK-A/BとCoreS3も0.3.0へ更新します。
2. CoreS3の `0.3.0-dual` と、1111〜7777の7行を確認します。
3. まずAだけで7777の距離を確認し、次にBも起動して、7777行のA/B両列を確認します。
   ほかの固定側がいない欄は `--` で正常です。いない相手への失敗通知も記録されます。
4. **CoreS3のCOM**をTera Termで115200 bps / 8N1 / フロー制御なしで開きます。
   新しいファイルに、Binary ON、Timestamp OFF、Include screen buffer OFF、追記なしで60秒以上保存します。
5. A/B両方で7777が取れたら、1111〜6666を番号に合うBINへ更新して1台ずつ追加します。

成功例は `UWB_EVENT,port=A,...,type=RANGE,...,anchor_hex=7777,...,range_mm=...,status=0x00,profile=1`。
AとBが両方出ることを確認します。`anchor`は数値の10進表記、`anchor_hex`が基板の4桁番号です。
`range_mm=-1` は無効通知、`HEALTH` は状態通知で測距成功ではありません。
`nlos_raw` はSDKの生値で、遮蔽を断定する値としては使いません。
距離が出なければCoreS3のログと2DK・2BPの起動ログを保存してください。
**2DK/2BP自身のUSBログは3,000,000 bps / 8N1 / フロー制御なし**です。

## 無線設定

正本は `config/dual_7bp.json`。固定側・2DK-A/Bは同じJSONから生成します。
`confirmed_against_anchors=true` は**配布する固定側ソースとの設定一致**で、実機確認済みの意味ではありません。

| 項目 | 2DK-Aの系統 | 2DK-Bの系統 |
|---|---|---|
| 開始役MAC | 0x8888 | 0x9999 |
| session ID | 0x11223344 | 0x11223345 |
| channel | 5 | 9 |
| 測距開始役 | 2DK-A | 2DK-B |
| 応答役 | 各2BP | 各2BP |
| 相手リスト | 0x1111〜0x7777 | 同左 |
| 設定周期 | 500 ms | 500 ms |

1対多DS-TWR、SP3、SFD2、preamble9、BPRF、slot2400 RSTU、25 slots。
Static STS、vendor0x0708、IV010203040506、1 segment、STS length64。
1111〜7777の応答スロットは順に1〜7です。固定側はA/Bの2セッションを持ちます。
各2DKは1セッションで7相手を扱い、7セッションを作成する方式ではありません。

A/Bのアドレス・session ID・チャンネルを分け、同じ開始役として混同される設定を避けています。
2BP内の単一無線部は2セッションを切り替えるため、**両系統の競合・欠測・実効更新周期は実測が必要**です。
A/Bの測距時刻を同期する構成ではありません。
500 msは設定周期で、全相手の距離が常に2 Hzで成功する保証ではありません。

## 時刻とログ

1つの測距通知に複数相手が含まれると、それぞれをUARTで順番に送ります。
同じ通知由来の`callback_ms`・`uci_seq`は同じです。`queue_ms`が相手ごとに異なる場合があります。
7相手×2回/秒×48バイト×10bit = 6720 bit/s（片側、状態通知を除く）ですが、
ソフトUARTのスケジューリング待ちもあるため、`tx_drop`・`queue_ms`を確認します。
成功値を繰り返して新しい測距に見せることはしません。

`HEALTH,anchor=0`はセッション全体の状態通知です。Idle/異常時にそのセッションの全距離表示を無効化します。
3秒以上古い距離も表示しません。CoreS3の受信時刻はA/B共通のµs時計で記録します。
無線測距時刻とUART受信時刻は同じではなく、遅延とジッタの実測が別途必要です。
USBログは128行を保持して再試行し、長い停止で満杯なら新しい行を捨てて`log_queue_drop`に記録します。
