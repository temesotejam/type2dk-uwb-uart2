# 0.2.0 実測距試験版

Type2DK Rev.4.1 ×2とType2BP EVK Rev.4.1 ×1〜3用。
固定側BP1/BP2/BP3、2DK-A/Bの測距BINを同梱します。CoreS3も0.2.0-dualへ更新します。
まず固定側BP1だけでA/Bの距離を確認してください。手順はRANGING_SETUP.mdです。

2DKの両方はPIO13 TX、CoreS3 PORT A黄=A/白=B。加速度センサ取得なし。
CoreS3は各A/BのBP1〜BP3の距離とnLos rawを表示し、USBログには受信µs時刻を保存します。
USBの一時停止では128行を保持して再試行します。

ARMビルド、ホスト試験、起動ヘッダ/CRC検証済み。**実機のUWB測距・複数BP同時動作は未確認**です。
以前のselftest BINも切り分け用に含みますが、距離取得にはrange BINを使ってください。
SDK本体・DK6Programmerは含めていません。ライセンス通知はlicensesを参照してください。
