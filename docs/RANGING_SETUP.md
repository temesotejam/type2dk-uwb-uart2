# v0.4.0 / 1111〜8888・既存固定局互換版

## 書き込むもの

| 機器 | ファイル | 備考 |
|---|---|---|
| 移動側2DK-A | `2dk_A_range_v0.4.0.bin` | 論理ID 0050 |
| 移動側2DK-B | `2dk_B_range_v0.4.0.bin` | 論理ID 0051 |
| 固定7777 Type2BP | `2bp_7777_anchor_v0.4.0.bin` | responder slot 7 |
| 固定8888 Type2DK | `2dk_8888_anchor_v0.4.0.bin` | responder slot 8 |
| 固定1111〜6666 | **書き換えない** | 既存legacy firmware |
| CoreS3 | ブラウザページ | 8行表示版0.4.0-legacy8 |

UWB基板は従来のDK6 Windows GUIで「任意のBIN」を選び、書き込み・照合を実行します。2BP用BINと2DK用BINは交換できません。

## 無線互換設定

提供された1111用HEXを基準にしています。

- UWB controller address: `0x0000`
- session ID: `0x11223344`
- channel: 9
- SP3 / SFD2 / preamble9
- slots per ranging round: 25
- interval: 50 ms
- responder slot: 1111=1, 2222=2, ... 7777=7, 8888=8

A/Bのログ上IDは0050/0051ですが、旧固定局と互換にするためUWB上のcontroller addressは両方0x0000です。A/Bは同じ50 msセッションを使用し、Bの開始を25 ms遅らせます。この2-controller共存は実機未確認なので、最初は下記順序で確認します。

## 最初の確認手順

1. CoreS3をページから0.4.0-legacy8へ更新。
2. Aだけを書き換えて起動し、**既存1111だけ**で距離が出るか確認。
3. 7777を書き換えて追加し、Aから1111/7777の両方が取れるか確認。
4. 8888を書き換えて追加し、Aから1111/7777/8888が取れるか確認。
5. Bを書き換えて追加し、A/B両列が継続して更新されるか確認。
6. 問題なければ2222〜6666を1台ずつ追加。これらは書き換えません。

CoreS3 USBは115200 bps / 8N1 / フロー制御なし。成功ログ例は`UWB_EVENT,port=A,node=1,tag_id=0050,...,anchor_hex=1111,...,range_mm=...`です。

**注意:** バイナリの構造検証とホストテストは済んでいますが、A/Bを同じlegacy identityで同時運用するRF挙動は未検証です。片側だけなら取れるが両方で欠測する場合は、まずCoreS3ログとA/B自身の3,000,000 bps起動ログを保存してください。
