Type2DK-A/Bの両方をPIO_13 TX、CoreS3 PORT Aの黄色GPIO2をA RX・白色GPIO1をB RXとする初期試験版です。

**同梱の2DK BINは合成UART試験です。UWBは起動しません。**
CoreS3の画面は `UART TEST` と表示します。固定2BPとの実測は相手のプロフィール確認後です。

`type2dk-uwb-uart2-0.1.1-test.zip` にA/BのBIN・CoreS3結合BIN・配線手順・ログ仕様が入っています。
CoreS3結合BINの書き込み先は `0x0` です。

CoreS3 0.1.1-dualはUSBログの文字欠け対策です。ESP-IDFのUSB Serial/JTAGドライバを使い、
行単位で投入し、ログ共通連番・CRC・破棄理由別カウンタを追加しました。
0.1.0の実機ログでA/B同時UART受信は確認済みですが、今回のUSB修正は実機での再確認が必要です。
更新対象はCoreS3のみです。2DK A/BのBINは従来の0.1.0から変更していません。
