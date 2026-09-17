Type2DK-A/Bの両方をPIO_13 TX、CoreS3 PORT Aの黄色GPIO2をA RX・白色GPIO1をB RXとする初期試験版です。

**同梱の2DK BINは合成UART試験です。UWBは起動しません。**
CoreS3の画面は `UART TEST` と表示します。固定2BPとの実測は相手のプロフィール確認後です。

`type2dk-uwb-uart2-0.1.0-test.zip` にA/BのBIN・CoreS3結合BIN・配線手順・ログ仕様が入っています。
CoreS3結合BINの書き込み先は `0x0` です。
テストとコンパイル済みですが、この新しい2ポート構成の実機動作は未確認です。
