コンパイル方法

    make

(ただし/usr/include/linux/dvb/*.hのために kernel-headersパッケージが必要)

----------
各コマンドの概要

cmds/s2scan
 mplayer/gstreamer用のチャンネルリスト設定を生成. {t,z,a}scan のS2API版

 使い方:
   s2scan -hでヘルプ
   -b / -c をつけるとBS / CS110のスキャン, つけないと地上波のスキャン
   -lをつけると 物理チャンネル番号(ISDB-T:13-62, ISDB-S:BS1-23, CS2-24)のリストを
       ファイルかstdinから入力して 指定されたチャンネルだけをスキャンする.
   -lをつけないと全チャンネル範囲をスキャンする.

ex1. ISDB-Tの物理チャンネル13,20をスキャンして 結果をdvb-channels.confへ出力する.
   s2scan -l <<EOF  > dvb-channels.conf
   13
   20
   EOF

ex2. BSの全物理チャンネルとTSIDをスキャン
   s2scan -b -p -v > ~/.mplayer/channels.conf

 出力フォーマット:
  NHK教育1:DTV_DELIVERY_SYSTEM=8|DTV_FREQUENCY=473142857:3088
  BS朝日1:DTV_DELIVERY_SYSTEM=9|DTV_FREQUENCY=1049480|DTV_ISDBS_TS_ID=0x4010:151

 注意:
  BSとCS110は同時にはスキャンできないので別々に起動が必要.
  チャンネル名はSDTから得られたもので 重複する場合がある. 後で見直して修正が必要.
  (":"を含まない任意の文字列に変更可能)
  例えば サービスIDに基づくチャンネル名に変更したい場合は 下のようにすれば良い。
   sed -e 's/\([^:]*\):\([^:]*\):\([^:]*\)/# \1\nCS\3:\2:\3/' < in-file > out-file

  夜中などでチャンネルが放送していなければ当然情報も得られない.
  同じ周波数,TSの中に複数のサービス(いわゆるチャンネル)が含まれているが,
  NHK教育などで2～3番組編成などしている場合以外は 通常同じ内容となっている.(サービスIDが違うだけ)

-----------
cmds/restamp
 MurdocCutter等でsequence単位でカットし連結されたTSファイルの
 PTS,DTSを付け直すツール.
 つなぎ目を再生する時につかえたり つなぎ目をまたいだシークがうまくいかない
 問題を回避できる (アルファ版)
  ex. restamp < foo.ts > bar.ts

-----------

cmds/nidump
 PAT, PMTなどの情報を表示し,TSのpid構成を見る.
 ex. nitdump < ~/foo.ts

-----------

cmds/dumpts[2]
  TSパケットのヘッダ表示する.
  ex. dumpts < foo.ts | lv
 フォーマットは出力の先頭から
   PID CC ca-flag onになっているパケットflag 先頭4バイト
 dumpts2は行頭にパケットの開始アドレスも表示する。

-----------
cmds/dumpeid
  入力されるTSにおいて, 指定した時刻で放送されているイベントのEIDを表示する
  使い方については、dumpeid -h およびdumpeid.cの先頭部分を参照

-----------
cmds/fixpat
  PAT中のプログラムを指定した１つのみに変更する
  fixpat -h参照

-----------
cmds/ptsdump
  入力ストリームから、各PESのPTS/DTSとPCRの値と出現位置をリストアップする。
