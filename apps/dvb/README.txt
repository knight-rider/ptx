DVB用ツール集

配布の構成について
 ./cmds/                各種ユーティリティコマンド 主にストリーム解析用
 ./scripts/             各種ユーティリテイ スクリプト 主に録画用
 ./readme-cmds.txt       ./cmds/* の説明
 ./readme-scripts.txt     ./scripts/* の説明
 ./README.txt これ

 詳しくはreadm-*.txtと、各コマンドの-hオプション、ソースコードを参照


変更履歴
----------------
ver1.2
 scripts/gst1.2/
 - gstreamer <= 1.6で動作するように修正
 scripts/gst1.2/dvb_sched{_ev}3
 - 内部で使用するヘルパースクリプトの場所のconfigを改良
 cmds/
 - gcc >= 5.2でのビルドエラー修正

----------------
ver1.1
 scripts/gst1.2/
 - gstreamer 1.2系列用の移植 
 scripts/gst1.0/*
 - 削除, 1.2系のみサポート
 cmds/nitdump
  - データが収集できない場合があるバグを修正

-----------------
ver1.0
 scripts/gst1.0/{dvb_sched3, dvb_sched_ev3, dvbrec3.py, dvbevrec3.py}
  - gstreamer 1.0 系列用の移植(小変更)の追加
 cmds/fixpat
  - マルチプログラムのTSのPATを指定した1プログラムのみに変更

-----------------
ver0.0.99
 cmds/dumpeid
  - 日本語文字コードの変換エラーの修正

-----------------
