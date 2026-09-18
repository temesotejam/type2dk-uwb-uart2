# 0.4.0 / legacy 1111〜6666を残す8固定局版

既存1111〜6666を再書き込みせず、追加7777（Type2BP）と8888（Type2DK）を加える試験版です。A/Bの論理IDは0050/0051ですが、旧固定局互換のためUWB上はcontroller 0x0000、session 0x11223344、Ch9を共有します。

UWB基板用BINはA、B、7777、8888の4本を別途検証ビルドしています。GitHub Actions配布物はCoreS3用です。CoreS3は1111〜8888の8行表示に更新します。

提供された1111 HEXはSHA-256 `e53fa7e3f5de98e07bffdc1d3fa9f7fb7ca37df84bf6d8dcc2aa2a6a05900d04`で確認済みです。2222〜6666については同系列でアドレス/slotだけ異なる前提です。A/B同時RF動作は実機未確認です。
