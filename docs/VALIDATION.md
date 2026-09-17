# Hardware validation checklist

0.1.0の実機ログではA/Bの同時UART受信を確認しました。一方、USBログには文字欠けと破棄がありました。
0.1.1はUSB出力を変更した版で、修正後の実機確認は未完了です。

1. Aのみ/Bのみ/両方同時のUART試験。A/Bに対応したBINを使用し、各5件/秒程度で増えること。
2. 5分以上取得し `bad/missing/ring_drop/fifo_error/frame_error` の増分を確認。
3. Aだけ再起動してAのrestartsが増え、Bのデータが途切れないこと。
4. A/Bを入れ替えた際 `wrong_node` が増えること。勝手に位置A/Bを入れ替えないこと。
5. USBを閉じても画面で受信が続き、再接続後 `log_drop` で行落ちを区別できること。
   0.1.1は `usb_init=ESP_OK` を確認し、Tera TermのBinaryログを30秒以上保存します。
   接続が安定した区間で `log_write_drop/log_queue_drop/log_format_drop` が増えず、
   `python scripts/check_log.py teraterm.log` がCRC不正・ログ連番欠落を検出しないことを確認します。
   取得開始・終了時やUSB抜き差し中の途中行は、その境界として別に扱います。
6. 実際の2BPプロフィールを確認し、まず1台の2BPとAだけで測距。Bを加え、最後に複数2BPへ拡張。
7. 無線あり/なしでdropと `queue_ms/rx_span_us/max_isr_batch` を比較。

## 遅延測定で言えること

このログ単体で直接分かるのは通知から送信開始までの待ち（5ms刻み）と、Core側で見たフレーム内の時間です。
**SR040の真の測距時刻から中央受信までの絶対遅延は、このログ単体では測れません。**
初回検証ではCoreの受信時刻を観測時刻として保存し、未補正と明記します。
絶対遅延が必要になったら、QN9090の結果通知時に別の計測用GPIOを変化させるなどし、
共通のロジックアナライザまたは中央タイマでGPIOとUARTを同時計測します。
それでもSR040内部の測距→通知生成の時間は別途評価が必要です。

ESKF、IMU/ToFドライバ、剛体拘束やYaw推定はこの版には含めません。

## 2026-09-17のUSBログ調査

提供された `teraterm.log`（0.1.0-dual）は34,604バイト、108行で、6行に文字欠け・行結合がありました。
Core時刻208.398秒〜232.399秒の約24秒間で、各ポートの正常受信は1,035→1,155（各120件）、
UART側のmissing/bad/ring_drop/fifo_error/frame_errorは0でした。
全ポート共通の `log_drop` は482→669（187件増加）。ログには約15.6秒の空白もあります。
UART取得とPC向けログ出力の問題を分けて扱います。USB接続状態の操作履歴は不明です。

Arduino-ESP32 2.0.17のHWCDC ISRは `usb_serial_jtag_ll_write_txfifo` の戻り値を使わず
元のキュー項目を返却します。一方、ESP-IDF 4.4.7のドライバは書き込めなかった残りを保持します。
この差を根拠にUSBドライバを切り替えましたが、今回の全欠落の原因と実機で確定したわけではありません。

- [Arduino HWCDC 2.0.17](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/HWCDC.cpp)
- [ESP-IDF USB Serial/JTAG 4.4.7](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/usb_serial_jtag.c)
