# 実測距版 0.2.0 / Rev.4.1

対象: **Type2DK Rev.4.1 ×2、Type2BP EVK Rev.4.1 ×1〜3、CoreS3**。
2DK-A/BはそれぞれPIO13 TXからCoreS3へ送信し、固定側はType2BPです。
この版はビルドとホスト試験済みで、組み合わせた実機測距は未確認です。

## 書き込むファイル

| 機器 | BIN | 書き込み |
|---|---|---|
| 2DK-A | `2dk_A_range_v0.2.0.bin` | DK6 Windows GUI「任意のBIN」 |
| 2DK-B | `2dk_B_range_v0.2.0.bin` | 同上 |
| 固定側BP1 | `2bp_BP1_anchor_v0.2.0.bin` | 同じGUI、Type2BP側のCOMを選択 |
| 固定側BP2 | `2bp_BP2_anchor_v0.2.0.bin` | 同上。2台目がある場合 |
| 固定側BP3 | `2bp_BP3_anchor_v0.2.0.bin` | 同上。3台目がある場合 |
| CoreS3 | `cores3-dual-merged.bin` | ブラウザ書き込みページ |

Type2BP EVKもUSB-UART経由で**QN9090**へ書き込みます。2DK用BINとは交換できません。
これまでの[DK6 Windows GUI](https://temesotejam.github.io/type2dk-uwb-uart/tools/dk6-gui/type2dk-programmer-gui-v1.0.0.zip)で、
対象基板のCOMと「任意のBIN」を選び「書き込み・照合」を実行します。
対象COMを開いているTera Termを閉じ、基板をこれまでの書き込みモードにしてから実行します。
書き込み後は各基板の電源を入れ直してください。固定側はUSB給電だけで測距を始めます。
初回の2BP起動はSR150へのファームウェア転送・校正で時間がかかるため、30秒程度待ちます。

USB-UARTの書き込みに進めない場合は2BP側のGUIログを保存してください。
J-Linkを使用する場合はRev.4.1のTP31・QN9090へ、BINをアドレス0から書き込みます。
配布セットには書き込みツール本体を含めていません。

## 配線

| 信号 | 接続先 |
|---|---|
| 2DK-A PIO13（TP8 pin 2） | CoreS3 PORT A 黄 GPIO2 |
| 2DK-B PIO13（TP8 pin 2） | CoreS3 PORT A 白 GPIO1 |
| 2DK-A/B GND | CoreS3 PORT A 黒 GND |
| PORT A 赤5V | 接続しない |

2DKはそれぞれUSB給電、以前のI²C外付けプルアップは取り外します。
UARTは両方38400 bps / 8N1。固定2BPとの有線通信はありません。
加速度センサ・BLE・2DK間測距・UWBユーザーデータ転送は使いません。

## 最初の実機確認

1. **まずBP1を1台だけ**使い、2DK-A/BとCoreS3も上表の実測距版に更新します。
2. CoreS3画面で `0.2.0-dual` を確認します。旧試験BINが残ると `UART TEST` のままです。
3. 見通しのある状態で待ち、A/B両方の **BP1** に距離が出ることを確認します。
   BP2/BP3をまだ用意していなければ、その欄は `--` のままで正常です。
4. **CoreS3のCOM**をTera Termで115200 bps / 8N1 / フロー制御なしで開きます。
   新しいファイル名に、Binary ON、Timestamp OFF、Include screen buffer OFF、追記なしで60秒以上保存します。
5. BP1で測距できたらBP2、BP3を1台ずつ追加します。それぞれ違う番号のBINを使います。

確認するログは `UWB_EVENT,type=RANGE,anchor=1,range_mm=...,status=0x00,profile=1`。
`range_mm=-1` はその通知が距離として無効。`HEALTH` は状態通知で、測距成功ではありません。
`nlos_raw` はSDKの生値を保ち、0/1/255などの値から遮蔽を自動断定しません。
距離が出ない場合はCoreS3のログと、2DK・2BPの起動ログを保存してください。
**2DK/2BP自身のUSBログは3,000,000 bps / 8N1 / フロー制御なし**です。

## 共通設定

正本は `config/dual_3bp.json`。固定側の設定は同じJSONのA/Bセッション表を反転して生成します。
`confirmed_against_anchors=true` は**配布する固定側ソースとの設定一致**を示します。実機での確認済みフラグではありません。

| 対象 | 短縮MAC | A用session ID | B用session ID |
|---|---|---|---|
| 2DK-A | 0x1101 | 各BPに対応 | — |
| 2DK-B | 0x1102 | — | 各BPに対応 |
| BP1 | 0x2101 | 0xA0000101 | 0xA0000201 |
| BP2 | 0x2102 | 0xA0000102 | 0xA0000202 |
| BP3 | 0x2103 | 0xA0000103 | 0xA0000203 |

Ch5、DS-TWR、SP1、SFD2、preamble10、BPRF、slot2400 RSTU、25 slots、周期1000 ms。
固定側=controller/initiator、2DK=controlee/responder。Static STS、vendor0x0708、IV010203040506、1 segment、STS length64。
Ranging control/report phaseと距離reportを有効にして、2DK側にも距離を通知します。
Static STSはこの試験用の共通設定です。

固定側1台はA/Bの2セッションを持ち、開始コマンド間に150msを置きます。
これは複数BP間の時刻同期や衝突回避を保証しません。複数BPの同時動作・実効更新周期は実測が必要です。
2DK1台は3セッションを持ち、いないBPへの待受を含みます。セッション停止・通知停止には個別再試行を行います。
通常動作に5分・30分などの終了時間はありません。

## USBログの保持

0.1.2の実機ログでは、操作なしの状態で約4秒・45行のUSBログ欠落がありました。
0.2.0はUSB投入が100msでタイムアウトしても、同じ行を保持して再試行します。
別タスクで128行をPSRAMへ保持し、UART取得と表示は継続します。
長くPCが読まない場合は新しい行を捨て、`log_queue_drop` と `log_seq` に残します。
`usb_waits` は100ms待っても投入できなかった回数、`log_pending` は生成時の待機行数です。
USB復帰後に古い行がまとまって届く場合も、`rx_first_us/rx_last_us` は元のUART受信時刻です。
この変更は一時停止による欠落を減らすためのもので、USB停止原因が実機で特定・解消したという意味ではありません。
