# Event wire v1 (48 bytes)

38400 baud / 8N1 / little endian。旧プロジェクトの96バイトwire v2とは非互換。
整数距離はSDKからの **cm** をそのまま保持。中央のmmは×10の単位変換であり、分解能は10mmのまま。

| offset | bytes | field |
|---:|---:|---|
| 0 | 4 | magic D2 55 45 33 |
| 4 | 1 | version=1 |
| 5 | 1 | type: 1 RANGE / 2 HEALTH / 3 TEST |
| 6 | 1 | node: A=1 / B=2 |
| 7 | 1 | flags bit0: measured distance valid; bit1: anchor profile confirmed |
| 8 | 4 | boot ID from hardware RNG |
| 12 | 4 | event sequence, global to this tag, including HEALTH/TEST |
| 16 | 4 | callback_ms: QN9090 notification entry RTOS time |
| 20 | 4 | tx_start_ms: immediately before encoding/transmitting |
| 24 | 4 | UCI ranging sequence, per session (only RANGE meaningful) |
| 28 | 4 | session ID |
| 32 | 2 | configured anchor ID |
| 34 | 2 | raw distance cm, FFFF unavailable |
| 36 | 1 | raw UWB result status; HEALTH/TEST=FF |
| 37 | 1 | raw nLos; HEALTH/TEST=FF |
| 38 | 1 | latest session state |
| 39 | 1 | latest session reason |
| 40 | 4 | cumulative TX queue/full/deadline drops |
| 44 | 1 | waiting queue depth at dequeue |
| 45 | 1 | fault: 0 none / 1 DWT timer / 2 worker / 3 reset / 4 unconfigured |
| 46 | 2 | CRC16-CCITT-FALSE over bytes 0..45 |

成功・失敗を含め受けた測距通知を1件ずつキューにコピーします。コールバック内では送信しません。
キュー16件、満杯時は新しいイベントを捨て、連番とdrop数を進めます。
250ms以上待ったイベントや250msを超えた送信を破棄し、古い値を現在値として再送しません。
UCI通知自体が来ない場合はRANGEを捏造しません。2秒周期のHEALTHでセッション状態を補います。
連番は32bitの巡回比較。再起動はboot IDで区別します。

## 時計

- `callback_ms`: **QN9090が結果通知を受けた時刻**。電波の測距時刻そのものではありません。
- `tx_start_ms`: QN9090内の送信開始近傍。上記との差が `queue_ms`（uint32差分でwrap対応）。
- QN9090は既存RTOSの200Hzを維持するため、両時刻の刻みは **5ms** です。
- `rx_first_us` / `rx_last_us`: **CoreS3のUART ISRが最初／最後のバイトを読んだ時刻**。
  両ポートとも `esp_timer_get_time()` を使用。USBログ行の出力時刻ではありません。
- FIFO割り込み閾値は1バイト。割り込み遅延のため厳密な電気的エッジ時刻ではありません。
  `max_isr_batch>1` の場合には複数バイトが溜まってから読まれています。
- `rx_span_us`: 最終バイト観測時刻−先頭バイト観測時刻。先頭1バイトの転送時間を含みません。
- 48バイトの純粋な線上時間は12.5ms。実装は8バイトごと（末尾を除く）に1RTOS tick譲るため、
  実際のフレーム時間はこれより長くなります。無線処理による追加遅延も実測対象です。

**A/Bのcallback_msを直接比較しないでください。** 時計の原点が異なります。
受信時刻と送信元時刻の単純な差も遅延ではありません。
片方向通信だけでは固定の時計オフセットと固定伝送遅延を分離できません。
時計変換 `t_central=a*t_tag+b` を推定しても、真の測距時刻までの遅延が自動的に求まるわけではありません。

## 状態と欠落

- `nlos_raw` はチップの通知値。遮蔽判定の有効性は未検証で、0をLOSの保証に使いません。
- `missing` は受信できた全イベント連番間の穴。送信側dropとUART損失の両方が含まれます。
- `uci_seq` はsessionごとに比較します。失敗ラウンド、通知欠落、機器の挙動を含むため、単独で原因を断定しません。
- `tx_drop`、Core側 `ring_drop` / `fifo_error` / `bad`、USB側 `log_drop` は分けます。
- `wrong_node` が増える場合はA/B配線または書き込んだBINの取り違えです。
- 3秒間新しい実測がない距離は画面から消します。HEALTHが来ても距離の鮮度は更新しません。
- CoreS3の画面は各タグの最新の1件を表示。全anchorのイベントはUSBに出力します。
- USBを閉じてもUART取得は継続。ログのキュー満杯やUSB未接続による行落ちは `log_drop` に記録します。
- `TEST` の raw_cm はテストパターン。距離として使えるフラグは付けません。
