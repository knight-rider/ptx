DVB用ツール集

配布の構成について
 ./cmd/			各種ユーティリティコマンド 主にストリーム解析用
 ./script/		各種ユーティリテイ スクリプト 主に録画用
 ./README.cmd		./cmd/* の説明
 ./README.script	./script/* の説明
 ./README.txt		これ

 詳しくは README.* と、各コマンドの-hオプション、ソースコードを参照


変更履歴
----------------
ver1.1
 script/gst1.2/
 - gstreamer 1.2系列用の移植 
 script/gst1.0/*
 - 削除, 1.2系のみサポート
 cmd/nitdump
  - データが収集できない場合があるバグを修正
-----------------
ver1.0
 script/gst1.0/{dvb_sched3, dvb_sched_ev3, dvbrec3.py, dvbevrec3.py}
  - gstreamer 1.0 系列用の移植(小変更)の追加
 cmd/fixpat
  - マルチプログラムのTSのPATを指定した1プログラムのみに変更
-----------------
ver0.0.99
 cmd/dumpeid
  - 日本語文字コードの変換エラーの修正
-----------------
